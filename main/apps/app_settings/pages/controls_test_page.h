/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

#include "../view/menu/menu_view.h"

namespace settings {

class ControlsTestPage final : public SettingsPage {
public:
    ~ControlsTestPage() override;

    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;

private:
    view::MenuView _menu_view;
    SettingsPageResult _result = SettingsPageResult::Active;
    bool _switch_value         = true;
    bool _long_switch_value    = false;
    int _slider_value          = 65;
    int _roller_value          = 0;
    int _segmented_value       = 0;
};

}  // namespace settings
