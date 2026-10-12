/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "controller.h"

#include <M5GFX.h>
#include <algorithm>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace screensaver {

namespace {
constexpr std::uint32_t IdleTimeout = 30 * 1000;
constexpr std::uint32_t DimTimeout  = 10 * 60 * 1000;
constexpr int DimBrightness         = 10;
}  // namespace

Controller::~Controller()
{
    stop();
}

void Controller::stop()
{
    if (_dimmed) {
        GetHAL().setBackLightBrightness(_restore_brightness);
        _dimmed = false;
    }
    _screensaver.reset();
    _preview_mode = false;
}

void Controller::reset(std::uint32_t now)
{
    stop();
    _last_activity    = now;
    _awaiting_release = false;
}

bool Controller::preview(std::uint32_t now)
{
    reset(now);
    const auto& registration = find(GetHAL().deviceSettings().screensaver);
    if (registration.create == nullptr) {
        mclog::tagWarn("Screensaver", "preview skipped: screensaver is off");
        return false;
    }
    _screensaver = registration.create();
    auto& canvas = GetHAL().gfxCanvas();
    _screensaver->init(canvas.width(), canvas.height(), now);
    _restore_brightness = GetHAL().getBackLightBrightness();
    _preview_mode       = true;
    mclog::tagInfo("Screensaver", "preview {}", registration.id);
    _screensaver->draw(canvas);
    GetHAL().presentGfx();
    return true;
}

bool Controller::update(std::uint32_t now, bool inputActive)
{
    if (inputActive) {
        _last_activity = now;
        if (active()) {
            stop();
            _awaiting_release = true;
            mclog::tagInfo("Screensaver", "wake");
        }
        return _awaiting_release;
    }
    if (_awaiting_release) {
        _last_activity    = now;
        _awaiting_release = false;
        return true;
    }

    const auto idle = now - _last_activity;
    bool redraw     = false;
    if (!active() && idle >= IdleTimeout) {
        const auto& registration = find(GetHAL().deviceSettings().screensaver);
        if (registration.create == nullptr) {
            return false;
        }
        _screensaver = registration.create();
        auto& canvas = GetHAL().gfxCanvas();
        _screensaver->init(canvas.width(), canvas.height(), now);
        _restore_brightness = GetHAL().getBackLightBrightness();
        redraw              = true;
        mclog::tagInfo("Screensaver", "start {}", registration.id);
    }
    if (!active()) {
        return false;
    }
    if (!_preview_mode && !_dimmed && idle >= DimTimeout) {
        _dimmed = GetHAL().setBackLightBrightness(std::min(_restore_brightness, DimBrightness));
        mclog::tagInfo("Screensaver", "dim after 10 minutes idle");
    }
    if (_screensaver->update(now) || redraw) {
        _screensaver->draw(GetHAL().gfxCanvas());
        GetHAL().presentGfx();
    }
    return true;
}

}  // namespace screensaver
