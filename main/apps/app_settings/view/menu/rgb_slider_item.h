/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "menu_item.h"

#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace settings::view {

class RgbSliderItem final : public MenuItem {
public:
    using ChangedCallback   = std::function<void(int)>;
    using CancelledCallback = std::function<void(int)>;

    static constexpr int PresetCount = 11;

    explicit RgbSliderItem(int index, ChangedCallback onChanged = {}, CancelledCallback onCancelled = {});

    ItemActivation activate() override;
    int preferredHeight() const override;
    bool adjust(int direction) override;
    void beginEdit() override;
    void commitEdit() override;
    void cancelEdit() override;
    bool update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const override;

    int index() const
    {
        return _index;
    }

    bool setIndex(int index, bool notifyChanged = true);
    static std::uint32_t presetColor(int index);

private:
    static int clampIndex(int index);

    int _index          = 0;
    int _snapshot_index = 0;
    ChangedCallback _on_changed;
    CancelledCallback _on_cancelled;
    uitk::AnimateValue _color_mix;
    uitk::AnimateValue _position;
    float _last_color_mix      = 0.0F;
    float _last_position       = 0.0F;
    int _last_adjust_direction = 0;
    bool _dirty                = true;
};

}  // namespace settings::view
