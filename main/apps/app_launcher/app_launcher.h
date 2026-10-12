/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/view.h"
#include <apps/common/screensaver/controller.h>
#include <memory>
#include <mooncake_templates.h>

class AppLauncher : public mooncake::templates::AppLauncherBase {
public:
    void onLauncherCreate() override;
    void onLauncherOpen() override;
    void onLauncherRunning() override;
    void onLauncherClose() override;
    void onLauncherDestroy() override;

private:
    void _create_view();

    std::unique_ptr<view::LauncherView> _view;
    input::KeyManager _key_manager;
    screensaver::Controller _screensaver;
    std::uint32_t _last_input_poll = 0;

#ifdef ESP_PLATFORM
    // Boot transition: the HAL shows a splash with the backlight off, then the
    // first launcher open fades the backlight up to the configured brightness,
    // holds the splash briefly, and finally builds the launcher UI.
    bool _boot_splash_pending      = true;
    bool _boot_fading              = false;
    bool _boot_holding             = false;
    int _boot_fade_target          = 0;
    std::uint32_t _boot_fade_start = 0;
    std::uint32_t _boot_hold_start = 0;
#endif
};
