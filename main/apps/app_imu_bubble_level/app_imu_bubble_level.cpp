/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_imu_bubble_level.h"

#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace {

constexpr std::uint32_t ImuUpdateIntervalMs = 16U;
constexpr std::uint8_t MagicTriggerCount    = 6U;

}  // namespace

AppImuBubbleLevel::AppImuBubbleLevel()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "IMU Bubble Level";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_imu_bubble_level);
    app_info.userData = &_theme_color;
}

void AppImuBubbleLevel::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppImuBubbleLevel::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    if (!GetHAL().setDisplayRenderer(DisplayRenderer::Gfx)) {
        mclog::tagError(getAppInfo().name, "failed to activate GFX renderer");
        close();
        return;
    }

    _last_imu_update_tick = GetHAL().millis();
    GetHAL().updateImuData();
    const ImuData initial_imu_data = GetHAL().getImuData();
    _latest_imu_data               = initial_imu_data;
    _magic_direction               = 0;
    _magic_count                   = 0;
    _magic_view.reset();

    _view = std::make_unique<view::ImuBubbleLevelView>();
    _view->init(initial_imu_data);
}

void AppImuBubbleLevel::onRunning()
{
    const input::KeyEvent event = _key_manager.update();
    if (event == input::KeyEvent::Home) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        close();
        return;
    }

    const std::uint32_t now = GetHAL().millis();
    if (_magic_view != nullptr) {
        if (event == input::KeyEvent::OkPressed && _magic_view->isGameOver()) {
            audio::play_ui_sfx(audio::UiSfx::Confirm);
            _magic_view->reset();
        }
    } else if (event == input::KeyEvent::Previous || event == input::KeyEvent::Next) {
        const int direction = event == input::KeyEvent::Previous ? -1 : 1;
        if (_magic_direction == 0 || direction != _magic_direction) {
            _magic_direction = direction;
            ++_magic_count;
        } else {
            _magic_direction = direction;
            _magic_count     = 1;
        }
        if (_magic_count >= MagicTriggerCount) {
            audio::play_ui_sfx(audio::UiSfx::Confirm);
            _view.reset();
            _magic_view = std::make_unique<view::MagicView>();
            _magic_view->init();
            _magic_count     = 0;
            _magic_direction = 0;
        }
    }

    bool imu_updated = false;
    ImuData imu_data = {};
    if (now - _last_imu_update_tick >= ImuUpdateIntervalMs) {
        _last_imu_update_tick = now;
        GetHAL().updateImuData();
        imu_data         = GetHAL().getImuData();
        _latest_imu_data = imu_data;
        imu_updated      = true;
    }

    if (_magic_view != nullptr) {
        _magic_view->update(now, _latest_imu_data);
        _magic_view->draw(GetHAL().gfxCanvas());
        GetHAL().presentGfx();
        return;
    }

    if (_view == nullptr) {
        return;
    }

    if (event == input::KeyEvent::OkPressed) {
        audio::play_ui_sfx(audio::UiSfx::Confirm);
        _view->toggleDataLabels();
    }

    if (imu_updated) {
        _view->setImuData(imu_data);
    }
    _view->update(now);
}

void AppImuBubbleLevel::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _view.reset();
    _magic_view.reset();
    _last_imu_update_tick = 0;
}
