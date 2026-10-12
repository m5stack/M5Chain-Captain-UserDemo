/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "debugger_receiver.h"

#include <ArduinoJson.h>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <mooncake_log.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <zmq.h>

namespace {

constexpr std::string_view Tag                   = "Debugger";
constexpr std::string_view DefaultEndpoint       = "tcp://127.0.0.1:5557";
constexpr int PollTimeoutMs                      = 100;
constexpr std::size_t MaximumChainDevicesPerSide = 12;

std::optional<debugger::BatteryPowerSource> parsePowerSource(std::string_view value)
{
    if (value == "battery") {
        return debugger::BatteryPowerSource::Battery;
    }
    if (value == "type_c") {
        return debugger::BatteryPowerSource::TypeC;
    }
    if (value == "chain") {
        return debugger::BatteryPowerSource::Chain;
    }
    if (value == "unknown") {
        return debugger::BatteryPowerSource::Unknown;
    }
    return std::nullopt;
}

std::optional<debugger::WifiSignal> parseWifiSignal(std::string_view value)
{
    if (value == "off") {
        return debugger::WifiSignal::Off;
    }
    if (value == "low") {
        return debugger::WifiSignal::Low;
    }
    if (value == "medium") {
        return debugger::WifiSignal::Medium;
    }
    if (value == "high") {
        return debugger::WifiSignal::High;
    }
    if (value == "failed") {
        return debugger::WifiSignal::Failed;
    }
    return std::nullopt;
}

std::optional<debugger::ButtonId> parseButtonId(std::string_view value)
{
    if (value == "power") {
        return debugger::ButtonId::Power;
    }
    if (value == "a") {
        return debugger::ButtonId::A;
    }
    if (value == "b") {
        return debugger::ButtonId::B;
    }
    if (value == "c") {
        return debugger::ButtonId::C;
    }
    return std::nullopt;
}

std::optional<debugger::ChainDeviceKind> parseChainDeviceKind(std::string_view value)
{
    if (value == "key") return debugger::ChainDeviceKind::Key;
    if (value == "angle") return debugger::ChainDeviceKind::Angle;
    if (value == "joystick") return debugger::ChainDeviceKind::Joystick;
    if (value == "buzzer") return debugger::ChainDeviceKind::Buzzer;
    if (value == "encoder") return debugger::ChainDeviceKind::Encoder;
    if (value == "mono") return debugger::ChainDeviceKind::Mono;
    if (value == "rgb") return debugger::ChainDeviceKind::Rgb;
    if (value == "pir") return debugger::ChainDeviceKind::Pir;
    if (value == "tof") return debugger::ChainDeviceKind::Tof;
    return std::nullopt;
}

bool isNumber(ArduinoJson::JsonVariantConst value)
{
    return value.is<float>() || value.is<double>() || value.is<int>();
}

bool parseJoystickAxis(ArduinoJson::JsonVariantConst value, std::optional<std::int16_t>& output)
{
    if (value.isUnbound()) return true;
    if (!value.is<int>()) return false;
    const int axis = value.as<int>();
    if (axis < -127 || axis > 127) return false;
    output = static_cast<std::int16_t>(axis);
    return true;
}

bool parseOptionalInt(ArduinoJson::JsonVariantConst value, std::int32_t minimum, std::int32_t maximum,
                      std::optional<std::int32_t>& output)
{
    output.reset();
    if (value.isUnbound()) return true;
    if (!value.is<int>()) return false;
    const auto number = value.as<std::int32_t>();
    if (number < minimum || number > maximum) return false;
    output = number;
    return true;
}

bool parseOptionalBool(ArduinoJson::JsonVariantConst value, std::optional<bool>& output)
{
    if (value.isUnbound()) return true;
    if (!value.is<bool>()) return false;
    output = value.as<bool>();
    return true;
}

bool parseChainSide(ArduinoJson::JsonVariantConst value, std::vector<debugger::ChainDeviceState>& output,
                    std::unordered_set<std::uint32_t>& ids)
{
    if (!value.is<ArduinoJson::JsonArrayConst>()) return false;
    const auto devices = value.as<ArduinoJson::JsonArrayConst>();
    if (devices.size() > MaximumChainDevicesPerSide) return false;

    output.reserve(devices.size());
    for (const ArduinoJson::JsonObjectConst device : devices) {
        if (!isNumber(device["id"]) || !device["kind"].is<const char*>()) return false;
        const auto raw_id = device["id"].as<std::int64_t>();
        const auto kind   = parseChainDeviceKind(device["kind"].as<const char*>());
        if (raw_id <= 0 || raw_id > UINT32_MAX || !kind.has_value()) return false;

        debugger::ChainDeviceState state;
        state.id   = static_cast<std::uint32_t>(raw_id);
        state.kind = *kind;
        if (state.kind == debugger::ChainDeviceKind::Joystick) {
            if (!parseJoystickAxis(device["x"], state.x) || !parseJoystickAxis(device["y"], state.y)) return false;
        }
        if (state.kind == debugger::ChainDeviceKind::Joystick || state.kind == debugger::ChainDeviceKind::Encoder) {
            if (!parseOptionalBool(device["pressed"], state.pressed)) return false;
        }
        std::optional<std::int32_t> number;
        if (!parseOptionalInt(device["value"], -32768, 32767, number)) return false;
        state.value = number;
        if (!parseOptionalInt(device["delta"], -32768, 32767, number)) return false;
        state.delta = number;
        if (!parseOptionalInt(device["distance"], 0, 65535, number)) return false;
        state.distance = number;
        if (!parseOptionalInt(device["frequency"], 0, 10000, number)) return false;
        if (number.has_value()) state.frequency = static_cast<std::uint16_t>(*number);
        if (!parseOptionalInt(device["mode"], 0, 255, number)) return false;
        if (number.has_value()) state.mode = static_cast<std::uint8_t>(*number);
        if (!parseOptionalInt(device["duty"], 0, 100, number)) return false;
        if (number.has_value()) state.duty = static_cast<std::uint8_t>(*number);
        const int maximum_brightness = state.kind == debugger::ChainDeviceKind::Mono ? 7 : 255;
        if (!parseOptionalInt(device["brightness"], 0, maximum_brightness, number)) return false;
        if (number.has_value()) state.brightness = static_cast<std::uint8_t>(*number);
        if (!parseOptionalBool(device["enabled"], state.enabled)) return false;
        if (!parseOptionalBool(device["detected"], state.detected)) return false;
        if (!ids.insert(state.id).second) return false;
        output.push_back(state);
    }
    return true;
}

std::string makeReply(std::string_view id, bool ok, std::string_view error = {})
{
    ArduinoJson::JsonDocument reply;
    reply["version"] = 1;
    reply["id"]      = id;
    reply["ok"]      = ok;
    if (!error.empty()) {
        reply["error"] = error;
    }

    std::string output;
    ArduinoJson::serializeJson(reply, output);
    return output;
}

}  // namespace

namespace debugger {

class DebuggerReceiver::Impl {
public:
    ~Impl()
    {
        stop();
    }

    void start()
    {
        if (_running.exchange(true)) {
            return;
        }
        _thread = std::thread([this]() { run(); });
    }

    void stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_thread.joinable()) {
            _thread.join();
        }
    }

    bool consumeStatusState(StatusState& state)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending_status_state.has_value()) {
            return false;
        }
        state = *_pending_status_state;
        _pending_status_state.reset();
        return true;
    }

    bool consumeButtonUpdate(ButtonUpdate& update)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pending_button_updates.empty()) {
            return false;
        }
        update = _pending_button_updates.front();
        _pending_button_updates.pop_front();
        return true;
    }

    bool consumeImuState(ImuState& state)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending_imu_state.has_value()) {
            return false;
        }
        state = *_pending_imu_state;
        _pending_imu_state.reset();
        return true;
    }

    bool consumeChainState(ChainState& state)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_pending_chain_state.has_value()) return false;
        state = std::move(*_pending_chain_state);
        _pending_chain_state.reset();
        return true;
    }

private:
    void run()
    {
        const char* configured_endpoint = std::getenv("CHAIN_CAPTAIN_DEBUGGER_ENDPOINT");
        const std::string endpoint =
            configured_endpoint == nullptr ? std::string(DefaultEndpoint) : configured_endpoint;

        void* context = zmq_ctx_new();
        void* socket  = context == nullptr ? nullptr : zmq_socket(context, ZMQ_REP);
        if (socket == nullptr) {
            mclog::tagError(Tag, "failed to create ZeroMQ receiver");
            closeSocket(context, socket);
            return;
        }

        const int linger = 0;
        zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
        if (zmq_bind(socket, endpoint.c_str()) != 0) {
            mclog::tagError(Tag, "failed to bind {}: {}", endpoint, zmq_strerror(zmq_errno()));
            closeSocket(context, socket);
            return;
        }

        mclog::tagInfo(Tag, "listening on {}", endpoint);
        while (_running.load()) {
            zmq_pollitem_t item = {.socket = socket, .fd = 0, .events = ZMQ_POLLIN, .revents = 0};
            const int result    = zmq_poll(&item, 1, PollTimeoutMs);
            if (result < 0) {
                if (zmq_errno() != EINTR) {
                    mclog::tagError(Tag, "poll failed: {}", zmq_strerror(zmq_errno()));
                }
                continue;
            }
            if (result == 0 || (item.revents & ZMQ_POLLIN) == 0) {
                continue;
            }
            receive(socket);
        }

        closeSocket(context, socket);
    }

    void receive(void* socket)
    {
        zmq_msg_t message;
        zmq_msg_init(&message);
        const int size = zmq_msg_recv(&message, socket, 0);
        if (size < 0) {
            zmq_msg_close(&message);
            return;
        }

        const auto* data = static_cast<const char*>(zmq_msg_data(&message));
        const std::string payload(data, static_cast<std::size_t>(size));
        zmq_msg_close(&message);

        const std::string reply = handleMessage(payload);
        zmq_send(socket, reply.data(), reply.size(), 0);
    }

    std::string handleMessage(const std::string& payload)
    {
        ArduinoJson::JsonDocument message;
        const auto error = ArduinoJson::deserializeJson(message, payload);
        if (error) {
            return makeReply("", false, "invalid_json");
        }

        const std::string id = message["id"] | "";
        if ((message["version"] | 0) != 1) {
            return makeReply(id, false, "unsupported_version");
        }

        const std::string type = message["type"] | "";
        if (type == "ping") {
            return makeReply(id, true);
        }
        if (type == "button") {
            const ArduinoJson::JsonObjectConst data = message["data"].as<ArduinoJson::JsonObjectConst>();
            if (data.isNull() || !data["button"].is<const char*>() || !data["pressed"].is<bool>()) {
                return makeReply(id, false, "invalid_button_data");
            }

            const auto button = parseButtonId(data["button"].as<const char*>());
            if (!button.has_value()) {
                return makeReply(id, false, "invalid_button");
            }

            {
                std::lock_guard<std::mutex> lock(_mutex);
                _pending_button_updates.push_back({.button = *button, .pressed = data["pressed"].as<bool>()});
            }
            return makeReply(id, true);
        }
        if (type == "imu") {
            const ArduinoJson::JsonObjectConst data = message["data"].as<ArduinoJson::JsonObjectConst>();
            if (data.isNull() || !isNumber(data["accel_x"]) || !isNumber(data["accel_y"]) ||
                !isNumber(data["accel_z"]) || !isNumber(data["gyro_x"]) || !isNumber(data["gyro_y"]) ||
                !isNumber(data["gyro_z"])) {
                return makeReply(id, false, "invalid_imu_data");
            }

            ImuState state;
            state.accel_x = data["accel_x"].as<float>();
            state.accel_y = data["accel_y"].as<float>();
            state.accel_z = data["accel_z"].as<float>();
            state.gyro_x  = data["gyro_x"].as<float>();
            state.gyro_y  = data["gyro_y"].as<float>();
            state.gyro_z  = data["gyro_z"].as<float>();
            if (!std::isfinite(state.accel_x) || !std::isfinite(state.accel_y) || !std::isfinite(state.accel_z) ||
                !std::isfinite(state.gyro_x) || !std::isfinite(state.gyro_y) || !std::isfinite(state.gyro_z) ||
                std::abs(state.accel_x) > 16.0f || std::abs(state.accel_y) > 16.0f || std::abs(state.accel_z) > 16.0f ||
                std::abs(state.gyro_x) > 4000.0f || std::abs(state.gyro_y) > 4000.0f ||
                std::abs(state.gyro_z) > 4000.0f) {
                return makeReply(id, false, "imu_data_out_of_range");
            }

            {
                std::lock_guard<std::mutex> lock(_mutex);
                _pending_imu_state = state;
            }
            return makeReply(id, true);
        }
        if (type == "chain") {
            const ArduinoJson::JsonObjectConst data = message["data"].as<ArduinoJson::JsonObjectConst>();
            if (data.isNull()) return makeReply(id, false, "invalid_chain_data");

            ChainState state;
            std::unordered_set<std::uint32_t> ids;
            if (!parseChainSide(data["left"], state.left, ids) || !parseChainSide(data["right"], state.right, ids)) {
                return makeReply(id, false, "invalid_chain_data");
            }
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _pending_chain_state = std::move(state);
            }
            return makeReply(id, true);
        }
        if (type != "status") {
            return makeReply(id, false, "unknown_type");
        }

        const ArduinoJson::JsonObjectConst data = message["data"].as<ArduinoJson::JsonObjectConst>();
        if (data.isNull() || !data["level"].is<int>() || !data["voltage_mv"].is<int>() ||
            !data["input_voltage_mv"].is<int>() || !data["power_source"].is<const char*>() ||
            !data["charging"].is<bool>() || !data["charging_enabled"].is<bool>()) {
            return makeReply(id, false, "invalid_status_data");
        }

        const int level            = data["level"].as<int>();
        const int voltage_mv       = data["voltage_mv"].as<int>();
        const int input_voltage_mv = data["input_voltage_mv"].as<int>();
        if (level < 0 || level > 100 || voltage_mv < 2500 || voltage_mv > 5000 || input_voltage_mv < 0 ||
            input_voltage_mv > 30000) {
            return makeReply(id, false, "status_data_out_of_range");
        }
        const auto power_source = parsePowerSource(data["power_source"].as<const char*>());
        if (!power_source.has_value()) {
            return makeReply(id, false, "invalid_power_source");
        }

        if (!data["wifi_signal"].is<const char*>()) {
            return makeReply(id, false, "invalid_status_data");
        }
        const auto wifi_signal = parseWifiSignal(data["wifi_signal"].as<const char*>());
        if (!wifi_signal.has_value()) {
            return makeReply(id, false, "invalid_wifi_signal");
        }

        StatusState state;
        state.level            = level;
        state.voltage_mv       = static_cast<std::uint16_t>(voltage_mv);
        state.input_voltage_mv = static_cast<std::uint16_t>(input_voltage_mv);
        state.power_source     = *power_source;
        state.charging         = data["charging"].as<bool>();
        state.charging_enabled = data["charging_enabled"].as<bool>();
        state.wifi_signal      = *wifi_signal;

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pending_status_state = state;
        }
        return makeReply(id, true);
    }

    static void closeSocket(void* context, void* socket)
    {
        if (socket != nullptr) {
            zmq_close(socket);
        }
        if (context != nullptr) {
            zmq_ctx_term(context);
        }
    }

    std::atomic<bool> _running = false;
    std::thread _thread;
    std::mutex _mutex;
    std::optional<StatusState> _pending_status_state;
    std::optional<ImuState> _pending_imu_state;
    std::optional<ChainState> _pending_chain_state;
    std::deque<ButtonUpdate> _pending_button_updates;
};

DebuggerReceiver::DebuggerReceiver() : _impl(std::make_unique<Impl>())
{
}

DebuggerReceiver::~DebuggerReceiver() = default;

void DebuggerReceiver::start()
{
    _impl->start();
}

bool DebuggerReceiver::consumeStatusState(StatusState& state)
{
    return _impl->consumeStatusState(state);
}

bool DebuggerReceiver::consumeImuState(ImuState& state)
{
    return _impl->consumeImuState(state);
}

bool DebuggerReceiver::consumeChainState(ChainState& state)
{
    return _impl->consumeChainState(state);
}

bool DebuggerReceiver::consumeButtonUpdate(ButtonUpdate& update)
{
    return _impl->consumeButtonUpdate(update);
}

}  // namespace debugger
