/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

#include "../view/menu/menu_view.h"

#include <cstdint>
#include <hal/hal.h>

namespace settings {

class DateTimeEditorPage final : public SettingsPage {
public:
    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;

private:
    enum class Stage : std::uint8_t {
        Year,
        Month,
        Day,
        Hour,
        Minute,
    };

    void buildStage();
    void updateStageValue(int index);
    void advanceStage();
    void previousStage();
    bool apply();

    view::MenuView _menu_view;
    DateTime _draft;
    Stage _stage                  = Stage::Year;
    int _preferred_day            = 1;
    SettingsPageResult _result    = SettingsPageResult::Active;
    bool _advance_stage_requested = false;
};

}  // namespace settings
