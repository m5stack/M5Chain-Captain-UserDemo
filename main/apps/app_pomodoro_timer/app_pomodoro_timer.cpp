/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_pomodoro_timer.h"

#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr std::array<std::uint32_t, 3> TimerDurationsMs = {
    25U * 60U * 1000U,
    5U * 60U * 1000U,
    15U * 60U * 1000U,
};
constexpr std::uint32_t ImuUpdateIntervalMs    = 16U;
constexpr float Pi                             = 3.14159265358979323846F;
constexpr float MinimumAccelMagnitude          = 0.25F;
constexpr float VerticalModeEnterZRatio        = 0.55F;
constexpr float VerticalModeExitZRatio         = 0.70F;
constexpr float BaselineSwitchThresholdDegrees = 55.0F;

std::size_t panelIndex(view::PomodoroPanel panel)
{
    return static_cast<std::size_t>(panel) - 1;
}

view::PomodoroPanel panelForBaseline(float baseline)
{
    int normalized = static_cast<int>(std::lround(baseline)) % 360;
    if (normalized < 0) {
        normalized += 360;
    }

    switch (normalized) {
        case 0:
            return view::PomodoroPanel::Pomodoro;
        case 270:
            return view::PomodoroPanel::ShortBreak;
        case 90:
            return view::PomodoroPanel::LongBreak;
        default:
            return view::PomodoroPanel::None;
    }
}

}  // namespace

AppPomodoroTimer::AppPomodoroTimer()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "Pomodoro Timer";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_pomodoro_timer);
    app_info.userData = &_theme_color;
}

void AppPomodoroTimer::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppPomodoroTimer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _alarm.stop();
    resetTimerState(GetHAL().millis());
    if (!GetHAL().setDisplayRenderer(DisplayRenderer::Gfx)) {
        mclog::tagError(getAppInfo().name, "failed to activate GFX renderer");
        close();
        return;
    }

    _view = std::make_unique<view::PomodoroTimerView>();
    _view->init();
}

void AppPomodoroTimer::onRunning()
{
    const input::KeyEvent event = _key_manager.update();
    if (event == input::KeyEvent::Home) {
        stopAlarm();
        audio::play_ui_sfx(audio::UiSfx::Back);
        close();
        return;
    }

    const std::uint32_t now = GetHAL().millis();
    updateCountdown(now);

    bool imu_updated = false;
    float tilt_x     = 0.0F;
    float tilt_y     = 0.0F;
    if (now - _last_imu_update_tick >= ImuUpdateIntervalMs) {
        _last_imu_update_tick = now;
        GetHAL().updateImuData();
        const ImuData& imu_data                = GetHAL().getImuData();
        const view::PomodoroPanel active_panel = updateOrientation(imu_data);
        handlePanelTransition(active_panel, now);

        const float magnitude = std::sqrt(imu_data.accelX * imu_data.accelX + imu_data.accelY * imu_data.accelY +
                                          imu_data.accelZ * imu_data.accelZ);
        if (magnitude >= MinimumAccelMagnitude) {
            tilt_x = std::clamp(imu_data.accelX / magnitude, -1.0F, 1.0F);
            tilt_y = std::clamp(imu_data.accelY / magnitude, -1.0F, 1.0F);
        }
        imu_updated = true;
    }
#if CHAIN_CAPTAIN_POMODORO_DEBUG
    if (event == input::KeyEvent::OkPressed) {
        if (_active_panel != view::PomodoroPanel::None) {
            audio::play_ui_sfx(audio::UiSfx::Confirm);
        }
        debugSkipToEnd(now);
    }
#endif
    _alarm.update(now);

    if (_view == nullptr) {
        return;
    }

    _view->setActivePanel(_active_panel);
    if (imu_updated) {
        _view->setIdleTilt(tilt_x, tilt_y);
    }
    for (std::size_t index = 0; index < _remaining_ms.size(); ++index) {
        const std::uint32_t remaining_seconds = (_remaining_ms[index] + 999U) / 1000U;
        const auto panel                      = static_cast<view::PomodoroPanel>(index + 1);
        _view->setRemainingSeconds(panel, remaining_seconds);
        _view->setTimerFinished(panel, _timer_finished[index]);
    }
    _view->update(now);
}

void AppPomodoroTimer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    stopAlarm();
    _view.reset();
}

void AppPomodoroTimer::resetTimerState(std::uint32_t now)
{
    _remaining_ms = TimerDurationsMs;
    _timer_finished.fill(false);
    _active_panel               = view::PomodoroPanel::None;
    _paused_panel               = view::PomodoroPanel::None;
    _alarm_panel                = view::PomodoroPanel::None;
    _last_countdown_tick        = now;
    _last_imu_update_tick       = now - ImuUpdateIntervalMs;
    _locked_baseline            = 0.0F;
    _vertical_mode              = false;
    _baseline_initialized       = false;
    _countdown_tick_initialized = true;
}

void AppPomodoroTimer::resetPanelTimer(view::PomodoroPanel panel)
{
    if (panel == view::PomodoroPanel::None) {
        return;
    }
    const std::size_t index = panelIndex(panel);
    _remaining_ms[index]    = TimerDurationsMs[index];
    _timer_finished[index]  = false;
}

void AppPomodoroTimer::handlePanelTransition(view::PomodoroPanel next_panel, std::uint32_t now)
{
    if (next_panel == _active_panel) {
        return;
    }

    if (_alarm_panel != view::PomodoroPanel::None) {
        stopAlarm();
    }

    if (_active_panel != view::PomodoroPanel::None && next_panel == view::PomodoroPanel::None) {
        // Leaving an expanded timer pauses it so a brief tilt does not lose progress.
        _paused_panel = _active_panel;
    } else if (_active_panel == view::PomodoroPanel::None && next_panel != view::PomodoroPanel::None) {
        if (_paused_panel != view::PomodoroPanel::None && _paused_panel != next_panel) {
            resetPanelTimer(_paused_panel);
        }
        _paused_panel = view::PomodoroPanel::None;
    } else if (_active_panel != view::PomodoroPanel::None && next_panel != view::PomodoroPanel::None) {
        resetPanelTimer(_active_panel);
        _paused_panel = view::PomodoroPanel::None;
    }

    _active_panel        = next_panel;
    _last_countdown_tick = now;
}

void AppPomodoroTimer::updateCountdown(std::uint32_t now)
{
    if (!_countdown_tick_initialized) {
        _last_countdown_tick        = now;
        _countdown_tick_initialized = true;
        return;
    }

    const std::uint32_t elapsed = now - _last_countdown_tick;
    _last_countdown_tick        = now;
    if (_active_panel == view::PomodoroPanel::None) {
        return;
    }

    std::uint32_t& remaining = _remaining_ms[panelIndex(_active_panel)];
    if (remaining == 0U) {
        return;
    }

    if (elapsed >= remaining) {
        remaining               = 0U;
        const std::size_t index = panelIndex(_active_panel);
        if (!_timer_finished[index]) {
            _timer_finished[index] = true;
            _alarm_panel           = _active_panel;
            _alarm.start(now);
        }
        return;
    }

    remaining -= elapsed;
}

void AppPomodoroTimer::stopAlarm()
{
    _alarm.stop();
    _alarm_panel = view::PomodoroPanel::None;
}

#if CHAIN_CAPTAIN_POMODORO_DEBUG
void AppPomodoroTimer::debugSkipToEnd(std::uint32_t now)
{
    if (_active_panel == view::PomodoroPanel::None) {
        return;
    }

    const std::size_t index = panelIndex(_active_panel);
    _remaining_ms[index]    = 5000U;
    _timer_finished[index]  = false;
    stopAlarm();
    _last_countdown_tick = now;
}
#endif

view::PomodoroPanel AppPomodoroTimer::updateOrientation(const ImuData& data)
{
    const float magnitude =
        std::sqrt(data.accelX * data.accelX + data.accelY * data.accelY + data.accelZ * data.accelZ);
    if (magnitude < MinimumAccelMagnitude) {
        return _active_panel;
    }

    const float z_ratio = std::abs(data.accelZ) / magnitude;
    if (!_vertical_mode) {
        if (z_ratio > VerticalModeEnterZRatio) {
            return view::PomodoroPanel::None;
        }
        _vertical_mode        = true;
        _baseline_initialized = false;
    } else if (z_ratio >= VerticalModeExitZRatio) {
        _vertical_mode        = false;
        _baseline_initialized = false;
        return view::PomodoroPanel::None;
    }

    const float orientation = std::atan2(-data.accelX, -data.accelY) * 180.0F / Pi;
    const float candidate   = std::round(orientation / 90.0F) * 90.0F;
    if (!_baseline_initialized) {
        _locked_baseline      = candidate;
        _baseline_initialized = true;
    } else if (std::abs(std::remainder(orientation - _locked_baseline, 360.0F)) >= BaselineSwitchThresholdDegrees) {
        _locked_baseline = candidate;
    }

    return panelForBaseline(_locked_baseline);
}
