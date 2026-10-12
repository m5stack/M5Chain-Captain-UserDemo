/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <M5PM1.h>
#include <driver/gpio.h>

namespace {

constexpr std::uint32_t DebounceMs = 10;
constexpr std::uint32_t PwrPollMs  = 10;
constexpr gpio_num_t ButtonAPin    = GPIO_NUM_1;
constexpr gpio_num_t ButtonBPin    = GPIO_NUM_4;
constexpr gpio_num_t ButtonCPin    = GPIO_NUM_5;

std::uint32_t last_pwr_poll = 0;
bool pwr_pressed            = false;

}  // namespace

namespace hal::internal {

esp_err_t buttonInit()
{
    gpio_config_t config = {};
    config.pin_bit_mask  = (1ULL << ButtonAPin) | (1ULL << ButtonBPin) | (1ULL << ButtonCPin);
    config.mode          = GPIO_MODE_INPUT;
    config.pull_up_en    = GPIO_PULLUP_ENABLE;
    config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    config.intr_type     = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}

}  // namespace hal::internal

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
    if (now - last_pwr_poll >= PwrPollMs) {
        last_pwr_poll      = now;
        bool current_state = pwr_pressed;
        auto* device       = hal::internal::pmic();
        if (device != nullptr && device->btnGetState(&current_state) == M5PM1_OK) {
            pwr_pressed = current_state;
        } else {
            pwr_pressed = btnPwr.isPressed();
        }
    }

    btnPwr.update(now, pwr_pressed);
    btnA.update(now, gpio_get_level(ButtonAPin) == 0);
    btnB.update(now, gpio_get_level(ButtonBPin) == 0);
    btnC.update(now, gpio_get_level(ButtonCPin) == 0);
}
