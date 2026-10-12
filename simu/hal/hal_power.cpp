/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

int Hal::getBatteryLevel()
{
    return _battery_level;
}

std::uint16_t Hal::getBatteryVoltage()
{
    return _battery_voltage;
}

std::uint16_t Hal::getInputVoltage()
{
    return _input_voltage;
}

PowerSource Hal::getPowerSource()
{
    return _power_source;
}

bool Hal::isBatteryCharging()
{
    return _battery_charging;
}

bool Hal::setBatteryChargingEnabled(bool enabled)
{
    _battery_charging_enabled = enabled;
    if (!enabled) {
        _battery_charging = false;
    }
    return true;
}

bool Hal::isBatteryChargingEnabled()
{
    return _battery_charging_enabled;
}

bool Hal::setPortPowerEnabled(bool enabled)
{
    _port_power_enabled = enabled;
    return true;
}

bool Hal::setPowerButtonDoubleClickEnabled(bool enabled)
{
    if (_power_button_double_click_enabled == enabled) {
        return true;
    }

    _power_button_double_click_enabled = enabled;
    return true;
}

bool Hal::setIrPowerEnabled(bool enabled)
{
    if (!enabled) {
        endIrRaw();
    }
    _ir_power_enabled = enabled;
    return true;
}
