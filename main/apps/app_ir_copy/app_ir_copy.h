/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "ir_signal.h"
#include "view/view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mooncake.h>

class AppIrCopy : public mooncake::AppAbility {
public:
    AppIrCopy();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void handleSlotClicked(std::size_t index);
    void handleSlotLongPressed(std::size_t index);
    void updateNavigation(std::uint32_t currentTime);
    void updateButtonGestures(std::uint32_t currentTime);
    void updateCapture();
    bool startCapture(std::size_t index);
    bool sendSignal(const ir_copy::Signal& signal);
    void cancelCapture();
    void loadStoredSignals();
    bool persistSignal(std::size_t index);

    static constexpr std::size_t NoSlot = ir_copy::SlotCount;

    std::uint32_t _theme_color = ir_copy::BackgroundColor;
    std::unique_ptr<ir_copy::IrCopyView> _view;
    std::array<ir_copy::Signal, ir_copy::SlotCount> _signals;
    std::size_t _pending_capture_index   = NoSlot;
    std::size_t _pressed_slot            = NoSlot;
    std::uint32_t _pressed_at            = 0;
    bool _long_press_consumed            = false;
    int _navigation_direction            = 0;
    std::uint32_t _navigation_started_at = 0;
    std::uint32_t _last_navigation_at    = 0;
};
