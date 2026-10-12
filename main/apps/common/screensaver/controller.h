/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "screensaver.h"

namespace screensaver {

class Controller {
public:
    ~Controller();
    void reset(std::uint32_t now);
    // Start the selected screensaver immediately, without waiting for idle time.
    bool preview(std::uint32_t now);
    // True means the saver (or its wake gesture) owns this input/frame.
    bool update(std::uint32_t now, bool inputActive);
    bool active() const
    {
        return _screensaver != nullptr;
    }

private:
    void stop();
    std::unique_ptr<Screensaver> _screensaver;
    std::uint32_t _last_activity = 0;
    int _restore_brightness      = 100;
    bool _dimmed                 = false;
    bool _preview_mode           = false;
    bool _awaiting_release       = false;
};

}  // namespace screensaver
