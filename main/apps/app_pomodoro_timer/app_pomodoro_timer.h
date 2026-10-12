/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "pomodoro_alarm.h"
#include "view/view.h"

#include <apps/common/key_manager/key_manager.h>
#include <array>
#include <cstdint>
#include <memory>
#include <mooncake.h>

#ifndef CHAIN_CAPTAIN_POMODORO_DEBUG
#define CHAIN_CAPTAIN_POMODORO_DEBUG 0
#endif

struct ImuData;

class AppPomodoroTimer : public mooncake::AppAbility {
public:
    AppPomodoroTimer();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void resetTimerState(std::uint32_t now);
    void resetPanelTimer(view::PomodoroPanel panel);
    void handlePanelTransition(view::PomodoroPanel next_panel, std::uint32_t now);
    void updateCountdown(std::uint32_t now);
    void stopAlarm();
#if CHAIN_CAPTAIN_POMODORO_DEBUG
    void debugSkipToEnd(std::uint32_t now);
#endif
    view::PomodoroPanel updateOrientation(const ImuData& data);

    std::uint32_t _theme_color = 0xFDFCE7;
    std::unique_ptr<view::PomodoroTimerView> _view;
    input::KeyManager _key_manager;
    std::array<std::uint32_t, 3> _remaining_ms = {};
    std::array<bool, 3> _timer_finished        = {};
    view::PomodoroPanel _active_panel          = view::PomodoroPanel::None;
    view::PomodoroPanel _paused_panel          = view::PomodoroPanel::None;
    view::PomodoroPanel _alarm_panel           = view::PomodoroPanel::None;
    PomodoroAlarm _alarm;
    std::uint32_t _last_countdown_tick  = 0;
    std::uint32_t _last_imu_update_tick = 0;
    float _locked_baseline              = 0.0F;
    bool _vertical_mode                 = false;
    bool _baseline_initialized          = false;
    bool _countdown_tick_initialized    = false;
};
