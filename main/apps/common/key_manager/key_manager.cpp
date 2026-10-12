/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_manager.h"

#include <hal/hal.h>

using namespace input;

namespace {

constexpr std::uint32_t HoldDelay          = 400;
constexpr std::uint32_t RepeatInterval     = 70;
constexpr std::uint32_t FastRepeatAfter    = 1200;
constexpr std::uint32_t FastRepeatInterval = 35;

}  // namespace

const KeyEvent& KeyManager::update()
{
    const std::uint32_t now = GetHAL().millis();
    GetHAL().updateButtonStates();

    const bool previous_pressed = GetHAL().btnB.isPressed();
    const bool next_pressed     = GetHAL().btnC.isPressed();
    const int direction         = previous_pressed == next_pressed ? 0 : (previous_pressed ? -1 : 1);
    if (direction != _repeat_direction) {
        _repeat_direction = direction;
        _hold_started_at  = now;
        _last_repeat_at   = now;
    }

    _event = KeyEvent::None;
    if (GetHAL().btnPwr.wasPressed()) {
        _event = KeyEvent::Home;
    } else if (GetHAL().btnA.wasPressed()) {
        _event = KeyEvent::OkPressed;
    } else if (GetHAL().btnA.wasReleased()) {
        _event = KeyEvent::OkReleased;
    } else if (GetHAL().btnB.wasPressed()) {
        _event = KeyEvent::Previous;
    } else if (GetHAL().btnC.wasPressed()) {
        _event = KeyEvent::Next;
    } else if (_repeat_direction != 0 && now - _hold_started_at >= HoldDelay) {
        const std::uint32_t repeat_interval =
            now - _hold_started_at >= FastRepeatAfter ? FastRepeatInterval : RepeatInterval;
        if (now - _last_repeat_at >= repeat_interval) {
            _last_repeat_at = now;
            _event          = _repeat_direction < 0 ? KeyEvent::PreviousRepeat : KeyEvent::NextRepeat;
        }
    }

    return _event;
}
