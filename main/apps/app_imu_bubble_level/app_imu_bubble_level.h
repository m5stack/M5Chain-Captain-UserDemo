/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "view/magic_view.h"
#include "view/view.h"

#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <memory>
#include <mooncake.h>

class AppImuBubbleLevel : public mooncake::AppAbility {
public:
    AppImuBubbleLevel();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::uint32_t _theme_color = 0xFFA879;
    std::unique_ptr<view::ImuBubbleLevelView> _view;
    std::unique_ptr<view::MagicView> _magic_view;
    input::KeyManager _key_manager;
    std::uint32_t _last_imu_update_tick = 0;
    ImuData _latest_imu_data;
    int _magic_direction      = 0;
    std::uint8_t _magic_count = 0;
};
