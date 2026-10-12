/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <esp_mac.h>
#include <esp_timer.h>
#include <mooncake_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

constexpr char Tag[] = "HAL-WIFI";

constexpr std::uint32_t ConfigExitDelayMs       = 500;
constexpr std::uint32_t ConnectionTimeoutMs     = 60'000;
constexpr std::uint32_t SignalRefreshIntervalMs = 1'000;

enum class DesiredMode : std::uint8_t {
    Off,
    Station,
    ConfigAp,
};

bool deadlineReached(std::uint32_t now, std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

std::uint32_t monotonicMillis()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1'000);
}

std::string makeHostname()
{
    std::uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "chain-captain";
    }

    char hostname[32] = {};
    std::snprintf(hostname, sizeof(hostname), "chain-captain-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return hostname;
}

WifiSignal signalFromRssi(int rssi)
{
    if (rssi >= 0) {
        return WifiSignal::Off;
    }
    if (rssi >= -65) {
        return WifiSignal::High;
    }
    if (rssi >= -75) {
        return WifiSignal::Medium;
    }
    return WifiSignal::Low;
}

std::uint64_t sessionGenerationFromEvent(const std::string& data)
{
    if (data.empty()) {
        return 0;
    }

    char* end                        = nullptr;
    const unsigned long long session = std::strtoull(data.c_str(), &end, 10);
    if (end == data.c_str() || *end != '\0') {
        return 0;
    }
    return static_cast<std::uint64_t>(session);
}

class WifiRuntime {
public:
    bool init()
    {
        std::lock_guard<std::mutex> control_lock(_control_mutex);

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_initialized) {
                return true;
            }
        }

        WifiManagerConfig config;
        config.ssid_prefix               = "Chain-Captain";
        config.language                  = "en-US";
        config.station_hostname          = makeHostname();
        config.station_failure_retry_cnt = 3;
        config.show_ota_config           = false;
        config.show_sleep_config         = false;

        auto& manager = WifiManager::GetInstance();
        if (!manager.Initialize(config)) {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            _status.state  = WifiState::Failed;
            _status.signal = WifiSignal::Failed;
            return false;
        }

        manager.SetEventCallback(
            [this](WifiEvent event, const std::string& data) { handleEvent(event, data, monotonicMillis()); });

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            _initialized = true;
            clearStatusLocked();
        }

        mclog::tagInfo(Tag, "initialized with hostname {}", config.station_hostname);
        return true;
    }

    void update(std::uint32_t now)
    {
        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return;
            }
        }

        std::lock_guard<std::mutex> control_lock(_control_mutex);
        bool restart_station             = false;
        bool refresh_signal              = false;
        std::uint32_t restart_generation = 0;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return;
            }

            if (_restart_pending && deadlineReached(now, _restart_at)) {
                _restart_pending = false;
                restart_station  = _desired_mode == DesiredMode::Station && !_station_active && !_config_active &&
                                  _restart_generation == _mode_generation;
                restart_generation = _restart_generation;
            }

            if (_connection_timeout_active && deadlineReached(now, _connection_timeout_at)) {
                _connection_timeout_active = false;
                if (_desired_mode == DesiredMode::Station && _status.state != WifiState::Connected) {
                    _connection_failure_latched = true;
                    _status.state               = WifiState::Failed;
                    _status.signal              = WifiSignal::Failed;
                    _status.rssi                = 0;
                    mclog::tagWarn(Tag, "station connection timed out");
                }
            }

            if (_desired_mode == DesiredMode::Station && _station_active && _status.state == WifiState::Connected &&
                deadlineReached(now, _signal_refresh_at)) {
                _signal_refresh_at = now + SignalRefreshIntervalMs;
                refresh_signal     = true;
            }
        }

        if (restart_station) {
            startStationForGenerationLocked(now, restart_generation);
        }
        if (refresh_signal) {
            refreshStationStatusLocked(now);
        }
    }

    bool startStation()
    {
        std::lock_guard<std::mutex> control_lock(_control_mutex);
        return startStationLocked(monotonicMillis());
    }

    bool stopStation()
    {
        std::lock_guard<std::mutex> control_lock(_control_mutex);
        bool should_stop = false;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return false;
            }
            if (_desired_mode == DesiredMode::ConfigAp) {
                return true;
            }

            ++_mode_generation;
            should_stop                = _station_active;
            _desired_mode              = DesiredMode::Off;
            _station_active            = false;
            _restart_pending           = false;
            _connection_timeout_active = false;
            clearStatusLocked();
        }

        if (should_stop) {
            WifiManager::GetInstance().StopStation();
        }
        return true;
    }

    bool startConfigAp()
    {
        std::lock_guard<std::mutex> control_lock(_control_mutex);
        std::uint32_t mode_generation = 0;
        bool config_claimed_active    = false;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return false;
            }
            config_claimed_active = _config_active;
        }

        if (config_claimed_active) {
            return WifiManager::GetInstance().IsConfigMode();
        }

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            mode_generation            = ++_mode_generation;
            _desired_mode              = DesiredMode::ConfigAp;
            _station_active            = false;
            _config_active             = false;
            _config_session_generation = 0;
            _restart_pending           = false;
            _connection_timeout_active = false;
            clearStatusLocked();
            _status.state = WifiState::Configuring;
        }

        auto& manager = WifiManager::GetInstance();
        manager.StartConfigAp();
        const bool config_active       = manager.IsConfigMode();
        const std::string ap_ssid      = config_active ? manager.GetApSsid() : std::string{};
        const std::string ap_url       = config_active ? manager.GetApWebUrl() : std::string{};
        const bool config_still_active = config_active && manager.IsConfigMode();
        bool started                   = false;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_mode_generation != mode_generation || _desired_mode != DesiredMode::ConfigAp) {
                return false;
            }

            if (config_still_active) {
                _station_active            = false;
                _config_active             = true;
                _restart_pending           = false;
                _connection_timeout_active = false;
                _status.state              = WifiState::Configuring;
                _status.signal             = WifiSignal::Off;
                _status.apSsid             = ap_ssid;
                _status.apWebUrl           = ap_url;
                started                    = true;
            } else {
                ++_mode_generation;
                _desired_mode              = DesiredMode::Off;
                _config_active             = false;
                _config_session_generation = 0;
                clearStatusLocked();
            }
        }

        if (!started) {
            mclog::tagError(Tag, "failed to start configuration AP");
            return false;
        }
        mclog::tagInfo(Tag, "configuration AP ready: {}, {}", ap_ssid, ap_url);
        return true;
    }

    bool stopConfigAp()
    {
        std::lock_guard<std::mutex> control_lock(_control_mutex);
        std::uint32_t mode_generation = 0;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return false;
            }
            if (_desired_mode != DesiredMode::ConfigAp && !_config_active) {
                return true;
            }

            mode_generation            = ++_mode_generation;
            _desired_mode              = DesiredMode::Off;
            _config_active             = false;
            _restart_pending           = false;
            _connection_timeout_active = false;
            clearStatusLocked();
        }

        WifiManager::GetInstance().StopConfigAp();
        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_mode_generation == mode_generation && _desired_mode == DesiredMode::Off) {
                _config_session_generation = 0;
            }
        }
        return true;
    }

    bool isInitialized() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _initialized;
    }

    bool isConfigMode() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _status.state == WifiState::Configuring;
    }

    WifiSignal signal() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _status.signal;
    }

    bool isConnected() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _status.state == WifiState::Connected;
    }

    WifiState state() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _status.state;
    }

    WifiStatus status() const
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        return _status;
    }

private:
    bool startStationLocked(std::uint32_t now)
    {
        bool stop_config              = false;
        std::uint32_t mode_generation = 0;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized) {
                return false;
            }
            if (_station_active) {
                return true;
            }
            if (_restart_pending && _desired_mode == DesiredMode::Station) {
                return true;
            }

            mode_generation  = ++_mode_generation;
            _desired_mode    = DesiredMode::Station;
            _restart_pending = false;
            stop_config      = _config_active;
            if (stop_config) {
                _config_active = false;
                clearStatusLocked();
            }
        }

        if (stop_config) {
            WifiManager::GetInstance().StopConfigAp();
            ensureStationRestartScheduled(monotonicMillis(), mode_generation);
            return true;
        }

        return startStationForGenerationLocked(now, mode_generation);
    }

    bool startStationForGenerationLocked(std::uint32_t now, std::uint32_t mode_generation)
    {
        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (!_initialized || _desired_mode != DesiredMode::Station || _mode_generation != mode_generation) {
                return false;
            }
            if (_station_active) {
                return true;
            }
            if (_config_active) {
                return false;
            }
        }

        if (SsidManager::GetInstance().GetSsidList().empty()) {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_desired_mode == DesiredMode::Station && _mode_generation == mode_generation) {
                ++_mode_generation;
                _desired_mode = DesiredMode::Off;
                clearStatusLocked();
            }
            mclog::tagWarn(Tag, "cannot start station without a saved network");
            return false;
        }

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_desired_mode != DesiredMode::Station || _mode_generation != mode_generation || _config_active) {
                return false;
            }
            _station_active            = true;
            _connection_timeout_active = true;
            _connection_timeout_at     = now + ConnectionTimeoutMs;
            _signal_refresh_at         = now;
            clearStatusLocked();
            _status.state = WifiState::Scanning;
        }

        auto& manager = WifiManager::GetInstance();
        manager.StartStation();
        manager.SetPowerSaveLevel(WifiPowerSaveLevel::BALANCED);
        mclog::tagInfo(Tag, "station started");
        return true;
    }

    void refreshStationStatusLocked(std::uint32_t now)
    {
        std::uint32_t event_generation = 0;
        bool read_connection_details   = false;

        {
            std::lock_guard<std::mutex> state_lock(_state_mutex);
            if (_desired_mode != DesiredMode::Station || !_station_active || _status.state != WifiState::Connected) {
                return;
            }
            event_generation        = _event_generation;
            read_connection_details = _connection_details_pending;
        }

        auto& manager          = WifiManager::GetInstance();
        const bool connected   = manager.IsConnected();
        const int rssi         = connected ? manager.GetRssi() : 0;
        const std::string ssid = connected && read_connection_details ? manager.GetSsid() : std::string{};
        const std::string ip   = connected && read_connection_details ? manager.GetIpAddress() : std::string{};

        std::lock_guard<std::mutex> state_lock(_state_mutex);
        if (_desired_mode != DesiredMode::Station || !_station_active || event_generation != _event_generation) {
            return;
        }
        if (!connected) {
            _status.state  = _connection_failure_latched ? WifiState::Failed : WifiState::Connecting;
            _status.signal = _connection_failure_latched ? WifiSignal::Failed : WifiSignal::Off;
            _status.rssi   = 0;
            _status.ipAddress.clear();
            if (!_connection_failure_latched && !_connection_timeout_active) {
                _connection_timeout_active = true;
                _connection_timeout_at     = now + ConnectionTimeoutMs;
            }
            _connection_details_pending = false;
            return;
        }

        _status.state  = WifiState::Connected;
        _status.signal = signalFromRssi(rssi);
        _status.rssi   = rssi;
        if (read_connection_details) {
            _status.ssid                = ssid;
            _status.ipAddress           = ip;
            _connection_details_pending = false;
        }
    }

    void handleEvent(WifiEvent event, const std::string& data, std::uint32_t now)
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        if (!_initialized) {
            return;
        }
        ++_event_generation;

        switch (event) {
            case WifiEvent::Scanning:
                if (_desired_mode == DesiredMode::Station && _station_active && !_connection_failure_latched) {
                    _status.state  = WifiState::Scanning;
                    _status.signal = WifiSignal::Off;
                }
                break;

            case WifiEvent::Connecting:
                if (_desired_mode == DesiredMode::Station && _station_active && !_connection_failure_latched) {
                    _status.state  = WifiState::Connecting;
                    _status.signal = WifiSignal::Off;
                    _status.ssid   = data;
                }
                break;

            case WifiEvent::Connected:
                if (_desired_mode == DesiredMode::Station && _station_active) {
                    _status.state               = WifiState::Connected;
                    _status.signal              = WifiSignal::Off;
                    _status.ssid                = data;
                    _connection_failure_latched = false;
                    _connection_timeout_active  = false;
                    _connection_details_pending = true;
                    _signal_refresh_at          = now;
                }
                break;

            case WifiEvent::Disconnected:
                if (_desired_mode == DesiredMode::Station && _station_active) {
                    _status.state  = _connection_failure_latched ? WifiState::Failed : WifiState::Connecting;
                    _status.signal = _connection_failure_latched ? WifiSignal::Failed : WifiSignal::Off;
                    _status.rssi   = 0;
                    _status.ipAddress.clear();
                    _connection_details_pending = false;
                    if (!_connection_failure_latched && !_connection_timeout_active) {
                        _connection_timeout_active = true;
                        _connection_timeout_at     = now + ConnectionTimeoutMs;
                    }
                } else if (_desired_mode == DesiredMode::Off) {
                    clearStatusLocked();
                }
                break;

            case WifiEvent::ConfigModeEnter:
                if (_desired_mode == DesiredMode::ConfigAp) {
                    _config_session_generation = sessionGenerationFromEvent(data);
                    _config_active             = true;
                    _status.state              = WifiState::Configuring;
                    _status.signal             = WifiSignal::Off;
                }
                break;

            case WifiEvent::ConfigModeExit:
                if (const auto session_generation = sessionGenerationFromEvent(data);
                    session_generation != 0 && session_generation != _config_session_generation) {
                    break;
                }
                if (_desired_mode == DesiredMode::ConfigAp) {
                    ++_mode_generation;
                    _desired_mode              = DesiredMode::Off;
                    _config_active             = false;
                    _config_session_generation = 0;
                    _station_active            = false;
                    _restart_pending           = false;
                    _connection_timeout_active = false;
                    clearStatusLocked();
                    break;
                } else if (_desired_mode == DesiredMode::Off) {
                    _config_active             = false;
                    _config_session_generation = 0;
                    _station_active            = false;
                    _restart_pending           = false;
                    _connection_timeout_active = false;
                    clearStatusLocked();
                    break;
                } else if (_desired_mode != DesiredMode::Station || _station_active || _restart_pending) {
                    break;
                }
                _config_active             = false;
                _config_session_generation = 0;
                _station_active            = false;
                _restart_pending           = true;
                _restart_generation        = _mode_generation;
                _restart_at                = now + ConfigExitDelayMs;
                clearStatusLocked();
                break;
        }
    }

    void clearStatusLocked()
    {
        _status                     = WifiStatus{};
        _connection_details_pending = false;
        _connection_failure_latched = false;
    }

    void ensureStationRestartScheduled(std::uint32_t now, std::uint32_t mode_generation)
    {
        std::lock_guard<std::mutex> state_lock(_state_mutex);
        if (_desired_mode == DesiredMode::Station && _mode_generation == mode_generation && !_station_active &&
            !_config_active && !_restart_pending) {
            _restart_pending    = true;
            _restart_generation = mode_generation;
            _restart_at         = now + ConfigExitDelayMs;
        }
    }

    mutable std::mutex _state_mutex;
    std::mutex _control_mutex;
    WifiStatus _status;
    DesiredMode _desired_mode                = DesiredMode::Off;
    bool _initialized                        = false;
    bool _station_active                     = false;
    bool _config_active                      = false;
    bool _restart_pending                    = false;
    bool _connection_timeout_active          = false;
    std::uint32_t _restart_at                = 0;
    std::uint32_t _connection_timeout_at     = 0;
    std::uint32_t _signal_refresh_at         = 0;
    std::uint32_t _event_generation          = 0;
    std::uint32_t _mode_generation           = 0;
    std::uint32_t _restart_generation        = 0;
    std::uint64_t _config_session_generation = 0;
    bool _connection_details_pending         = false;
    bool _connection_failure_latched         = false;
};

WifiRuntime& wifiRuntime()
{
    static WifiRuntime runtime;
    return runtime;
}

}  // namespace

namespace hal::internal {

void wifiUpdate(std::uint32_t now)
{
    wifiRuntime().update(now);
}

}  // namespace hal::internal

bool Hal::startWifiStation()
{
    if (!wifiRuntime().init()) {
        return false;
    }
    return wifiRuntime().startStation();
}

bool Hal::stopWifiStation()
{
    return wifiRuntime().stopStation();
}

bool Hal::startWifiConfigAp()
{
    if (!wifiRuntime().init()) {
        return false;
    }
    return wifiRuntime().startConfigAp();
}

bool Hal::stopWifiConfigAp()
{
    return wifiRuntime().stopConfigAp();
}

bool Hal::isWifiInitialized() const
{
    return wifiRuntime().isInitialized();
}

bool Hal::isWifiConfigMode() const
{
    return wifiRuntime().isConfigMode();
}

WifiSignal Hal::getWifiSignal() const
{
    return wifiRuntime().signal();
}

bool Hal::isWifiConnected() const
{
    return wifiRuntime().isConnected();
}

WifiState Hal::getWifiState() const
{
    return wifiRuntime().state();
}

WifiStatus Hal::getWifiStatus() const
{
    return wifiRuntime().status();
}
