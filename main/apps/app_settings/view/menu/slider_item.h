/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "menu_item.h"

#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace settings::view {

class SliderItem final : public MenuItem {
public:
    using ChangedCallback   = std::function<void(int)>;
    using CommittedCallback = std::function<bool(int)>;
    using CancelledCallback = std::function<void(int)>;

    SliderItem(std::string_view label, int value, int minimum, int maximum, int step, ChangedCallback onChanged = {},
               CommittedCallback onCommitted = {}, CancelledCallback onCancelled = {});

    ItemActivation activate() override;
    int preferredHeight() const override;
    bool adjust(int direction) override;
    void beginEdit() override;
    void commitEdit() override;
    void cancelEdit() override;
    bool update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const override;

    int value() const
    {
        return _value;
    }

    void setValue(int value, bool animated = true);

private:
    int clampAndQuantize(int value) const;
    float normalizedValue() const;
    bool setValueInternal(int value, bool animated, bool notifyChanged);

    int _minimum        = 0;
    int _maximum        = 100;
    int _step           = 1;
    int _value          = 0;
    int _value_snapshot = 0;
    ChangedCallback _on_changed;
    CommittedCallback _on_committed;
    CancelledCallback _on_cancelled;
    uitk::AnimateValue _progress;
    uitk::AnimateValue _edit_mix;
    float _last_progress = 0.0F;
    float _last_edit_mix = 0.0F;
};

}  // namespace settings::view
