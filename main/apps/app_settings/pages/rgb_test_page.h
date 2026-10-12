/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

#include "../view/menu/menu_view.h"

namespace settings::view {
class RgbSliderItem;
class SliderItem;
}  // namespace settings::view

namespace settings {

class RgbTestPage final : public SettingsPage {
public:
    ~RgbTestPage() override;

    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;
    bool redraw() override;

private:
    bool setChannel(int& channel, int value, view::SliderItem* slider);
    bool setPreset(int index);
    void syncChannelItems();
    bool applyColor() const;

    view::MenuView _menu_view;
    view::RgbSliderItem* _rgb_slider = nullptr;
    view::SliderItem* _red_slider    = nullptr;
    view::SliderItem* _green_slider  = nullptr;
    view::SliderItem* _blue_slider   = nullptr;
    SettingsPageResult _result       = SettingsPageResult::Active;
    int _red                         = 0;
    int _green                       = 0;
    int _blue                        = 0;
    bool _syncing_channel_items      = false;
};

}  // namespace settings
