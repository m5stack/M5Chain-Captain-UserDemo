/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <cstdio>
#include <M5GFX.h>

namespace {

constexpr std::uint32_t DebounceMs = 10;

}  // namespace

void Button::update(std::uint32_t now, bool pressed)
{
    _last_update  = now;
    _was_pressed  = false;
    _was_released = false;

    if (_raw_pressed != pressed) {
        _raw_pressed     = pressed;
        _last_raw_change = now;
    }
    if (_pressed == _raw_pressed || now - _last_raw_change < DebounceMs) {
        return;
    }

    _pressed      = _raw_pressed;
    _last_change  = now;
    _was_pressed  = _pressed;
    _was_released = !_pressed;
}

bool Button::pressedFor(std::uint32_t ms) const
{
    return _pressed && _last_update - _last_change >= ms;
}

void Hal::updateButtonStates()
{
    const std::uint32_t now = millis();
    btnPwr.update(now, _debug_btn_pwr_pressed || !lgfx::gpio_in(3));
    btnA.update(now, _debug_btn_a_pressed || !lgfx::gpio_in(0));
    btnB.update(now, _debug_btn_b_pressed || !lgfx::gpio_in(1));
    btnC.update(now, _debug_btn_c_pressed || !lgfx::gpio_in(2));
}
