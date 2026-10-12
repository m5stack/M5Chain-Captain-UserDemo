/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

namespace input {

enum class KeyEvent {
    None,
    Home,
    OkPressed,
    OkReleased,
    Previous,
    Next,
    PreviousRepeat,
    NextRepeat,
};

class KeyManager {
public:
    const KeyEvent& update();
    const KeyEvent& getEvent() const
    {
        return _event;
    }

private:
    KeyEvent _event                = KeyEvent::None;
    int _repeat_direction          = 0;
    std::uint32_t _hold_started_at = 0;
    std::uint32_t _last_repeat_at  = 0;
};

}  // namespace input
