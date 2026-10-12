/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

#include "../view/menu/menu_view.h"

#include <vector>

namespace settings {

class MenuPage final : public SettingsPage {
public:
    explicit MenuPage(std::vector<view::MenuSection> sections);
    ~MenuPage() override;

    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;
    bool redraw() override;
    TransitionSurface transitionSurface() const override;
    std::uint32_t transitionBackgroundColor() const override;

private:
    std::vector<view::MenuSection> _sections;
    view::MenuView _menu_view;
    SettingsPageResult _result = SettingsPageResult::Active;
    bool _background_only      = false;
};

}  // namespace settings
