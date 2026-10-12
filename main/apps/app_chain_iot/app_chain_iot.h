/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "theme.h"

#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <mooncake.h>
#include <memory>

namespace chain_iot::view {
class ChainMenuView;
}

class AppChainIot : public mooncake::AppAbility {
public:
    AppChainIot();
    ~AppChainIot() override;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::uint32_t _theme_color = chain_iot::ThemeColor;
    input::KeyManager _key_manager;
    std::unique_ptr<chain_iot::view::ChainMenuView> _view;
    std::uint32_t _next_snapshot_at              = 0;
    bool _power_button_double_click_was_enabled  = true;
    bool _power_button_double_click_guard_active = false;

    void updateSnapshot();
};
