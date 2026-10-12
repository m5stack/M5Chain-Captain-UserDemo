/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "menu_item.h"

#include <array>
#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <string>
#include <uitk/short_namespace.hpp>

namespace settings::view {

class SwitchItem final : public MenuItem {
public:
    using CommittedCallback = std::function<bool(bool)>;

    SwitchItem(std::string_view label, bool value, CommittedCallback onCommitted = {});

    ItemActivation activate() override;
    int preferredHeight() const override;
    bool update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const override;

    bool value() const
    {
        return _value;
    }

    void setValue(bool value, bool animated = true);

private:
    bool _value = false;
    CommittedCallback _on_committed;
    uitk::AnimateValue _state_mix;
    float _last_state_mix                  = 0.0F;
    mutable int _resolved_label_width      = -1;
    mutable std::uint8_t _label_font_size  = 20;
    mutable std::uint8_t _label_line_count = 1;
    mutable std::array<std::string, 2> _label_lines;
};

}  // namespace settings::view
