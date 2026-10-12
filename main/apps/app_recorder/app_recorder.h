/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mooncake.h>

class Button;

class AppRecorder : public mooncake::AppAbility {
public:
    AppRecorder();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void updateButtonGestures(std::uint32_t currentTime);
    void startRecording(std::size_t slot);
    void finishRecording();
    void failRecording();
    void startPlayback(std::size_t slot, std::uint32_t currentTime);
    void stopPlayback();
    Button& buttonForSlot(std::size_t slot);

    static constexpr std::size_t NoSlot = recorder::SlotCount;

    std::uint32_t _theme_color = recorder::AppThemeColor;
    std::unique_ptr<recorder::RecorderView> _view;
    std::array<bool, recorder::SlotCount> _recorded{};
    std::size_t _pressed_slot           = NoSlot;
    std::size_t _recording_slot         = NoSlot;
    std::size_t _playing_slot           = NoSlot;
    std::uint32_t _pressed_at           = 0;
    std::uint32_t _recording_started_at = 0;
    std::uint32_t _recording_closes_at  = 0;
    std::uint32_t _playback_closes_at   = 0;
    bool _long_press_consumed           = false;
    bool _recording_close_pending       = false;
    bool _playback_finished             = false;
};
