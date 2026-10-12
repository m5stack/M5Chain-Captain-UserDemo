/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"

#ifdef ESP_PLATFORM
#include <cmath>
#endif
#include <apps/common/audio/audio.h>
#include <hal/hal.h>
#include <mooncake_log.h>

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    GetHAL().setDisplayRenderer(DisplayRenderer::Gfx);
    _screensaver.reset(GetHAL().millis());
    _key_manager     = {};
    _last_input_poll = GetHAL().millis();

#ifdef ESP_PLATFORM
    if (_boot_splash_pending) {
        // First launch after boot: keep the HAL splash on screen while the
        // backlight fades up, then hold it briefly before revealing the launcher.
        _boot_splash_pending = false;
        _boot_fade_target    = GetHAL().getBackLightBrightness();
        _boot_fade_start     = GetHAL().millis();
        _boot_fading         = true;
    } else {
        _create_view();
    }
#else
    _create_view();
#endif
}

void AppLauncher::_create_view()
{
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    _view->onAppClicked = [this](int appId) {
        mclog::tagInfo(getAppInfo().name, "open app id: {}", appId);
        openApp(appId);
    };
}

void AppLauncher::onLauncherRunning()
{
#ifdef ESP_PLATFORM
    if (_boot_fading) {
        constexpr std::uint32_t BootFadeDurationMs = 500;
        const auto now                             = GetHAL().millis();
        const std::uint32_t elapsed                = now - _boot_fade_start;
        if (elapsed >= BootFadeDurationMs) {
            _boot_fading     = false;
            _boot_holding    = true;
            _boot_hold_start = now;
            GetHAL().setBackLightBrightness(_boot_fade_target);
        } else {
            const float t     = static_cast<float>(elapsed) / static_cast<float>(BootFadeDurationMs);
            const float eased = t * t * (3.0F - 2.0F * t);
            GetHAL().setBackLightBrightness(
                static_cast<int>(std::lround(eased * static_cast<float>(_boot_fade_target))));
        }
    } else if (_boot_holding) {
        constexpr std::uint32_t BootSplashHoldMs = 800;
        if (GetHAL().millis() - _boot_hold_start >= BootSplashHoldMs) {
            _boot_holding = false;
            _create_view();
            audio::play_ui_sfx(audio::UiSfx::Confirm);
        }
    }
#endif

    if (!_view) {
        return;
    }
    auto& hal             = GetHAL();
    const auto now        = hal.millis();
    const bool poll_input = now - _last_input_poll > _view->getConfig().readInputInterval;
    input::KeyEvent event = input::KeyEvent::None;
    if (poll_input) {
        event            = _key_manager.update();
        _last_input_poll = now;
    }
    const bool activity = event != input::KeyEvent::None || hal.btnPwr.isPressed() || hal.btnA.isPressed() ||
                          hal.btnB.isPressed() || hal.btnC.isPressed() || _view->isOpening() || _view->isPressing();
    const bool was_active = _screensaver.active();
    const bool consumed   = _screensaver.update(now, activity);
    if (was_active && !_screensaver.active()) {
        _view->redraw();
    }
    if (consumed) {
        return;
    }
    if (poll_input) {
        _view->handleInput(event);
    }
    _view->update(now);
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _screensaver.reset(GetHAL().millis());
    _view.reset();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on destroy");
}
