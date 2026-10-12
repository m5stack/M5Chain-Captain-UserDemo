/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <cstdint>

namespace settings {

enum class TransitionSurface {
    Snapshot,
    Background,
};

enum class SettingsPageResult {
    Active,
    Applied,
    Cancelled,
};

class SettingsPage {
public:
    virtual ~SettingsPage() = default;

    virtual void init()                             = 0;
    virtual void handleInput(input::KeyEvent event) = 0;
    virtual void update(std::uint32_t currentTime)  = 0;
    virtual SettingsPageResult result() const       = 0;
    virtual bool redraw()
    {
        return false;
    }
    virtual TransitionSurface transitionSurface() const
    {
        return TransitionSurface::Snapshot;
    }
    virtual std::uint32_t transitionBackgroundColor() const
    {
        return 0xFFFFFF;
    }
};

}  // namespace settings
