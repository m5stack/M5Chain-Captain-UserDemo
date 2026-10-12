/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "debugger/debugger_receiver.h"
#include "hal_internal.h"

#include <algorithm>
#include <cstdio>
#include <M5GFX.h>
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <mooncake_log.h>
#include <mutex>
#include <string_view>

namespace {

constexpr std::string_view Tag = "HAL";
std::unique_ptr<Hal> hal_instance;
M5GFX display;
std::unique_ptr<lgfx::LGFX_Sprite> gfx_canvas;
lv_display_t* lvgl_display = nullptr;
std::mutex lvgl_mutex;
std::uint32_t last_lvgl_tick;

bool isLeapYear(std::uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month)
{
    static constexpr std::array<std::uint8_t, 12> MonthDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return MonthDays[month - 1];
}

ChainDeviceData defaultChainData(ChainDeviceKind kind)
{
    const RgbColor color{0xAB, 0xCD, 0xEF};
    switch (kind) {
        case ChainDeviceKind::Key:
            return ChainKeyData{false, color};
        case ChainDeviceKind::Angle:
            return ChainAngleData{123, color};
        case ChainDeviceKind::Joystick:
            return ChainJoystickData{0, 0, false, color};
        case ChainDeviceKind::Buzzer: {
            ChainBuzzerData data;
            data.rgb       = color;
            data.frequency = 2700;
            data.duty      = 50;
            return data;
        }
        case ChainDeviceKind::Encoder: {
            ChainEncoderData data;
            data.rgb   = color;
            data.value = 42;
            data.delta = 1;
            return data;
        }
        case ChainDeviceKind::Mono: {
            ChainMonoData data;
            data.rgb        = {0xFF, 0xFF, 0xFF};
            data.brightness = 7;
            return data;
        }
        case ChainDeviceKind::Rgb: {
            ChainRgbData data;
            data.rgb        = color;
            data.brightness = 50;
            return data;
        }
        case ChainDeviceKind::Pir:
            return ChainPirData{color, false};
        case ChainDeviceKind::Tof:
            return ChainTofData{color, 123};
    }
    return ChainRgbData{color};
}

void lvgl_flush_cb(lv_display_t* lvgl_display, const lv_area_t* area, uint8_t* pixel_map)
{
    (void)area;
    (void)pixel_map;

    gfx_canvas->pushSprite(&display, 0, 0);

#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    GetHAL().recordDisplayFrame();
#endif

    lv_display_flush_ready(lvgl_display);
}

}  // namespace

namespace hal::internal {

bool audioInit();
void audioShutdown();
void recorderShutdown();

}  // namespace hal::internal

Hal& GetHAL()
{
    if (!hal_instance) {
        hal_instance = std::make_unique<Hal>();
    }
    return *hal_instance;
}

Hal::~Hal()
{
    hal::internal::recorderShutdown();
    hal::internal::audioShutdown();
}

bool DateTime::isValid() const
{
    const std::uint8_t max_day = daysInMonth(year, month);
    return year >= 2000 && year <= 2099 && max_day != 0 && day >= 1 && day <= max_day && hour < 24 && minute < 60 &&
           second < 60 && weekday < 7;
}

void Hal::init()
{
    mclog::tagInfo(Tag, "init");

    loadDeviceSettings();
    if (!setTimezone(_device_settings.timezone)) {
        mclog::tagWarn(Tag, "failed to apply timezone: {}", _device_settings.timezone);
    }
    if (!initFileSystem()) {
        mclog::tagWarn(Tag, "simulator file system unavailable; file-backed apps may be disabled");
    }

    constexpr int DisplayWidth  = 240;
    constexpr int DisplayHeight = 240;

    auto* panel          = new lgfx::Panel_sdl();
    auto config          = panel->config();
    config.memory_width  = DisplayWidth;
    config.panel_width   = DisplayWidth;
    config.memory_height = DisplayHeight;
    config.panel_height  = DisplayHeight;
    config.bus_shared    = false;
    panel->config(config);
    panel->setWindowTitle("Chain Captain Simulator");
    panel->setScaling(1, 1);

    display.init(panel);
    display.setRotation(_display_rotation);
    display.fillScreen(TFT_BLACK);

    gfx_canvas = std::make_unique<lgfx::LGFX_Sprite>(&display);
    gfx_canvas->setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
    if (gfx_canvas->createSprite(DisplayWidth, DisplayHeight) == nullptr) {
        mclog::tagError(Tag, "failed to allocate the shared display surface");
        std::abort();
    }
    gfx_canvas->fillScreen(TFT_BLACK);

    panel->addKeyCodeMapping(SDLK_q, 3);
    panel->addKeyCodeMapping(SDLK_e, 0);
    panel->addKeyCodeMapping(SDLK_a, 1);
    panel->addKeyCodeMapping(SDLK_d, 2);

    lv_init();
    lvgl_display = lv_display_create(DisplayWidth, DisplayHeight);
    lv_display_set_color_format(lvgl_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(lvgl_display, lvgl_flush_cb);

    constexpr std::size_t DrawBufferBytes = DisplayWidth * DisplayHeight * sizeof(std::uint16_t);
    lv_display_set_buffers(lvgl_display, gfx_canvas->getBuffer(), nullptr, DrawBufferBytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_timer_pause(lv_display_get_refr_timer(lvgl_display));

    last_lvgl_tick       = millis();
    _display_initialized = true;

    _wifi_initialized = true;
    _wifi_status      = WifiStatus{};
    initializeChainFixture();

    _debugger_receiver = std::make_unique<debugger::DebuggerReceiver>();
    _debugger_receiver->start();

    if (!hal::internal::audioInit()) {
        mclog::tagWarn(Tag, "simulator audio unavailable");
    }
    setSpeakerVolume(_device_settings.speakerVolume);
    setSpeakerMuted(false);
}

void Hal::delay(std::uint32_t ms)
{
    SDL_Delay(ms);
}

std::uint32_t Hal::millis()
{
    return SDL_GetTicks();
}

void Hal::feedTheDog()
{
    applyDebuggerUpdates();

    const std::uint32_t now = millis();
    hal::internal::ezdataUpdate(now);
    if (_display_renderer == DisplayRenderer::Lvgl) {
        lv_tick_inc(now - last_lvgl_tick);
    }
    last_lvgl_tick = now;

    if (_display_renderer == DisplayRenderer::Lvgl) {
        std::lock_guard<std::mutex> lock(lvgl_mutex);
        lv_timer_handler();
    }

#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    updateDisplayFps();
#endif
    delay(1);
}

bool Hal::beginChainEzData()
{
    return hal::internal::ezdataStart();
}

void Hal::endChainEzData()
{
    hal::internal::ezdataStop();
}

EzDataStatus Hal::getChainEzDataStatus() const
{
    return hal::internal::ezdataStatus();
}

void Hal::publishChainEzDataSnapshot(const ChainSnapshot& snapshot)
{
    hal::internal::ezdataPublishChainSnapshot(snapshot);
}

void Hal::applyDebuggerUpdates()
{
    if (_debugger_receiver == nullptr) {
        return;
    }

    debugger::StatusState state;
    if (_debugger_receiver->consumeStatusState(state)) {
        _battery_level            = state.level;
        _battery_voltage          = state.voltage_mv;
        _input_voltage            = state.input_voltage_mv;
        _battery_charging_enabled = state.charging_enabled;
        _battery_charging         = state.charging;
        WifiSignal wifi_signal    = WifiSignal::Off;
        switch (state.wifi_signal) {
            case debugger::WifiSignal::Low:
                wifi_signal = WifiSignal::Low;
                break;
            case debugger::WifiSignal::Medium:
                wifi_signal = WifiSignal::Medium;
                break;
            case debugger::WifiSignal::High:
                wifi_signal = WifiSignal::High;
                break;
            case debugger::WifiSignal::Failed:
                wifi_signal = WifiSignal::Failed;
                break;
            default:
                break;
        }
        setWifiSignalFromDebugger(wifi_signal);

        switch (state.power_source) {
            case debugger::BatteryPowerSource::Battery:
                _power_source = PowerSource::Battery;
                break;
            case debugger::BatteryPowerSource::TypeC:
                _power_source = PowerSource::TypeC;
                break;
            case debugger::BatteryPowerSource::Chain:
                _power_source = PowerSource::Chain;
                break;
            default:
                _power_source = PowerSource::Unknown;
                break;
        }

        mclog::tagInfo(Tag, "debug status: wifi={}, battery={}%, {} mV, charging={}",
                       static_cast<int>(_wifi_status.signal), _battery_level, _battery_voltage, _battery_charging);
    }

    debugger::ImuState imu_state;
    if (_debugger_receiver->consumeImuState(imu_state)) {
        _imu_data.accelX = imu_state.accel_x;
        _imu_data.accelY = imu_state.accel_y;
        _imu_data.accelZ = imu_state.accel_z;
        _imu_data.gyroX  = imu_state.gyro_x;
        _imu_data.gyroY  = imu_state.gyro_y;
        _imu_data.gyroZ  = imu_state.gyro_z;
    }

    debugger::ChainState chain_state;
    if (_debugger_receiver->consumeChainState(chain_state)) {
        const auto find_existing = [this](std::uint32_t id, ChainDeviceKind kind) -> const ChainDeviceSnapshot* {
            const auto find_in = [id, kind](const std::vector<ChainDeviceSnapshot>& devices) {
                const auto found = std::find_if(devices.begin(), devices.end(), [id, kind](const auto& device) {
                    return device.id == id && device.kind == kind;
                });
                return found == devices.end() ? nullptr : &*found;
            };
            if (const auto* device = find_in(_chain_left); device != nullptr) return device;
            return find_in(_chain_right);
        };
        const auto convert_side = [&find_existing](const std::vector<debugger::ChainDeviceState>& devices) {
            std::vector<ChainDeviceSnapshot> snapshots;
            snapshots.reserve(devices.size());
            for (const auto& device : devices) {
                ChainDeviceKind kind = ChainDeviceKind::Key;
                switch (device.kind) {
                    case debugger::ChainDeviceKind::Key:
                        kind = ChainDeviceKind::Key;
                        break;
                    case debugger::ChainDeviceKind::Angle:
                        kind = ChainDeviceKind::Angle;
                        break;
                    case debugger::ChainDeviceKind::Joystick:
                        kind = ChainDeviceKind::Joystick;
                        break;
                    case debugger::ChainDeviceKind::Buzzer:
                        kind = ChainDeviceKind::Buzzer;
                        break;
                    case debugger::ChainDeviceKind::Encoder:
                        kind = ChainDeviceKind::Encoder;
                        break;
                    case debugger::ChainDeviceKind::Mono:
                        kind = ChainDeviceKind::Mono;
                        break;
                    case debugger::ChainDeviceKind::Rgb:
                        kind = ChainDeviceKind::Rgb;
                        break;
                    case debugger::ChainDeviceKind::Pir:
                        kind = ChainDeviceKind::Pir;
                        break;
                    case debugger::ChainDeviceKind::Tof:
                        kind = ChainDeviceKind::Tof;
                        break;
                }
                if (const auto* existing = find_existing(device.id, kind); existing != nullptr)
                    snapshots.push_back(*existing);
                else
                    snapshots.push_back({device.id, kind, defaultChainData(kind)});
                if (auto* data = std::get_if<ChainJoystickData>(&snapshots.back().data); data != nullptr) {
                    if (device.x.has_value()) data->x = *device.x;
                    if (device.y.has_value()) data->y = *device.y;
                    if (device.pressed.has_value()) data->pressed = *device.pressed;
                }
                if (auto* data = std::get_if<ChainBuzzerData>(&snapshots.back().data); data != nullptr) {
                    if (device.frequency.has_value()) data->frequency = *device.frequency;
                    if (device.mode.has_value()) data->mode = *device.mode;
                    if (device.duty.has_value()) data->duty = *device.duty;
                    if (device.enabled.has_value()) data->enabled = *device.enabled;
                }
                if (auto* data = std::get_if<ChainEncoderData>(&snapshots.back().data); data != nullptr) {
                    if (device.value.has_value()) data->value = static_cast<std::int16_t>(*device.value);
                    if (device.delta.has_value()) data->delta = static_cast<std::int16_t>(*device.delta);
                    if (device.pressed.has_value()) data->pressed = *device.pressed;
                }
                if (auto* data = std::get_if<ChainMonoData>(&snapshots.back().data); data != nullptr) {
                    if (device.mode.has_value()) data->mode = *device.mode;
                    if (device.brightness.has_value()) {
                        data->brightness = std::min<std::uint8_t>(*device.brightness, 7);
                        const auto gray  = static_cast<std::uint8_t>(((data->brightness + 1) * 255 + 4) / 8);
                        data->rgb        = {gray, gray, gray};
                    }
                }
                if (auto* data = std::get_if<ChainRgbData>(&snapshots.back().data); data != nullptr) {
                    if (device.mode.has_value()) data->mode = *device.mode;
                    if (device.brightness.has_value()) data->brightness = *device.brightness;
                }
                if (auto* data = std::get_if<ChainPirData>(&snapshots.back().data);
                    data != nullptr && device.detected.has_value()) {
                    data->detected = *device.detected;
                }
                if (auto* data = std::get_if<ChainTofData>(&snapshots.back().data);
                    data != nullptr && device.distance.has_value()) {
                    data->distanceMm = static_cast<std::uint16_t>(*device.distance);
                }
            }
            return snapshots;
        };
        applyChainFixture(convert_side(chain_state.left), convert_side(chain_state.right));
    }

    debugger::ButtonUpdate button_update;
    if (_debugger_receiver->consumeButtonUpdate(button_update)) {
        switch (button_update.button) {
            case debugger::ButtonId::Power:
                _debug_btn_pwr_pressed = button_update.pressed;
                break;
            case debugger::ButtonId::A:
                _debug_btn_a_pressed = button_update.pressed;
                break;
            case debugger::ButtonId::B:
                _debug_btn_b_pressed = button_update.pressed;
                break;
            case debugger::ButtonId::C:
                _debug_btn_c_pressed = button_update.pressed;
                break;
        }
    }
}

bool Hal::lvglLock()
{
    lvgl_mutex.lock();
    return true;
}

void Hal::lvglUnlock()
{
    lvgl_mutex.unlock();
}

bool Hal::setDisplayRenderer(DisplayRenderer renderer)
{
    if (!_display_initialized) {
        return false;
    }

    std::lock_guard<std::mutex> lock(lvgl_mutex);
    if (_display_renderer == renderer) {
        return true;
    }

    auto* refresh_timer = lv_display_get_refr_timer(lvgl_display);
    if (renderer == DisplayRenderer::Gfx) {
        lv_timer_pause(refresh_timer);
    } else {
        lv_timer_resume(refresh_timer);
        lv_obj_invalidate(lv_display_get_screen_active(lvgl_display));
    }

    _display_renderer = renderer;
    last_lvgl_tick    = millis();
    return true;
}

lgfx::LGFX_Sprite& Hal::gfxCanvas()
{
    assert(gfx_canvas != nullptr && _display_renderer == DisplayRenderer::Gfx);
    return *gfx_canvas;
}

void Hal::presentGfx()
{
    if (!_display_initialized || _display_renderer != DisplayRenderer::Gfx || _gfx_present_suppressed) {
        return;
    }
    gfx_canvas->pushSprite(&display, 0, 0);

#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    GetHAL().recordDisplayFrame();
#endif
}

void Hal::setGfxPresentSuppressed(bool suppressed)
{
    _gfx_present_suppressed = suppressed;
}

bool Hal::startWifiStation()
{
    if (!_wifi_initialized) {
        mclog::tagWarn(Tag, "simulator Wi-Fi is not initialized");
        return false;
    }

    _wifi_status.state  = WifiState::Off;
    _wifi_status.signal = WifiSignal::Off;
    _wifi_status.rssi   = 0;
    _wifi_status.ssid.clear();
    _wifi_status.ipAddress.clear();
    _wifi_status.apSsid.clear();
    _wifi_status.apWebUrl.clear();
    mclog::tagInfo(Tag, "simulator Wi-Fi station start requested (no backend)");
    return true;
}

bool Hal::stopWifiStation()
{
    if (!_wifi_initialized) {
        return false;
    }

    if (_wifi_status.state != WifiState::Configuring) {
        _wifi_status.state  = WifiState::Off;
        _wifi_status.signal = WifiSignal::Off;
        _wifi_status.rssi   = 0;
        _wifi_status.ssid.clear();
        _wifi_status.ipAddress.clear();
    }
    return true;
}

bool Hal::startWifiConfigAp()
{
    if (!_wifi_initialized) {
        mclog::tagWarn(Tag, "simulator Wi-Fi is not initialized");
        return false;
    }

    _wifi_status.state  = WifiState::Configuring;
    _wifi_status.signal = WifiSignal::Off;
    _wifi_status.rssi   = 0;
    _wifi_status.ssid.clear();
    _wifi_status.ipAddress.clear();
    _wifi_status.apSsid   = "Chain-Captain-Setup";
    _wifi_status.apWebUrl = "http://192.168.4.1";
    mclog::tagInfo(Tag, "simulator Wi-Fi config AP requested");
    return true;
}

bool Hal::stopWifiConfigAp()
{
    if (!_wifi_initialized) {
        return false;
    }

    _wifi_status = WifiStatus{};
    return true;
}

void Hal::setWifiSignalFromDebugger(WifiSignal signal)
{
    if (_wifi_status.state == WifiState::Configuring) {
        if (signal == WifiSignal::Low || signal == WifiSignal::Medium || signal == WifiSignal::High) {
            _wifi_status = WifiStatus{};
            mclog::tagInfo(Tag, "simulator Wi-Fi configuration completed");
        }
        return;
    }
    _wifi_status.signal = signal;

    switch (signal) {
        case WifiSignal::Low:
            _wifi_status.state = WifiState::Connected;
            _wifi_status.rssi  = -80;
            break;
        case WifiSignal::Medium:
            _wifi_status.state = WifiState::Connected;
            _wifi_status.rssi  = -70;
            break;
        case WifiSignal::High:
            _wifi_status.state = WifiState::Connected;
            _wifi_status.rssi  = -55;
            break;
        case WifiSignal::Failed:
            _wifi_status.state = WifiState::Failed;
            _wifi_status.rssi  = 0;
            break;
        case WifiSignal::Off:
        default:
            _wifi_status.state = WifiState::Off;
            _wifi_status.rssi  = 0;
            break;
    }
}

bool Hal::setBackLightBrightness(int brightness)
{
    _backlight_brightness = brightness < 0 ? 0 : (brightness > 100 ? 100 : brightness);
    return true;
}

bool Hal::setDisplayRotation(int rotation)
{
    if (!_display_initialized || rotation < 0 || rotation > 3) {
        return false;
    }
    std::lock_guard<std::mutex> lock(lvgl_mutex);
    display.setRotation(rotation);
    _display_rotation = rotation;
    if (_display_renderer == DisplayRenderer::Lvgl) {
        lv_obj_invalidate(lv_display_get_screen_active(lvgl_display));
    }
    return true;
}
