/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace debugger {

enum class BatteryPowerSource : std::uint8_t {
    Unknown = 0,
    Battery,
    TypeC,
    Chain,
};

enum class WifiSignal : std::uint8_t {
    Off = 0,
    Low,
    Medium,
    High,
    Failed,
};

struct StatusState {
    int level                       = 75;
    std::uint16_t voltage_mv        = 3975;
    std::uint16_t input_voltage_mv  = 5000;
    BatteryPowerSource power_source = BatteryPowerSource::TypeC;
    bool charging                   = true;
    bool charging_enabled           = true;
    WifiSignal wifi_signal          = WifiSignal::Off;
};

struct ImuState {
    float accel_x = 0.0f;
    float accel_y = 0.0f;
    float accel_z = 1.0f;
    float gyro_x  = 0.0f;
    float gyro_y  = 0.0f;
    float gyro_z  = 0.0f;
};

enum class ChainDeviceKind : std::uint8_t {
    Key,
    Angle,
    Joystick,
    Buzzer,
    Encoder,
    Mono,
    Rgb,
    Pir,
    Tof,
};

struct ChainDeviceState {
    std::uint32_t id     = 0;
    ChainDeviceKind kind = ChainDeviceKind::Key;
    std::optional<std::int16_t> x;
    std::optional<std::int16_t> y;
    std::optional<bool> pressed;
    std::optional<std::int32_t> value;
    std::optional<std::int32_t> delta;
    std::optional<std::int32_t> distance;
    std::optional<std::uint16_t> frequency;
    std::optional<std::uint8_t> mode;
    std::optional<std::uint8_t> duty;
    std::optional<std::uint8_t> brightness;
    std::optional<bool> enabled;
    std::optional<bool> detected;
};

struct ChainState {
    // Nearest-to-farthest ordering matches the public HAL snapshot contract.
    std::vector<ChainDeviceState> left;
    std::vector<ChainDeviceState> right;
};

enum class ButtonId : std::uint8_t {
    Power = 0,
    A,
    B,
    C,
};

struct ButtonUpdate {
    ButtonId button = ButtonId::Power;
    bool pressed    = false;
};

class DebuggerReceiver {
public:
    DebuggerReceiver();
    ~DebuggerReceiver();

    DebuggerReceiver(const DebuggerReceiver&)            = delete;
    DebuggerReceiver& operator=(const DebuggerReceiver&) = delete;

    void start();
    bool consumeStatusState(StatusState& state);
    bool consumeImuState(ImuState& state);
    bool consumeChainState(ChainState& state);
    bool consumeButtonUpdate(ButtonUpdate& update);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace debugger
