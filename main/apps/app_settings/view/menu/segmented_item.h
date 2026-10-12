/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "menu_item.h"

#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <string_view>
#include <uitk/short_namespace.hpp>
#include <vector>

namespace settings::view {

class SegmentedItem final : public MenuItem {
public:
    using ChangedCallback   = std::function<void(int)>;
    using CommittedCallback = std::function<bool(int)>;
    using CancelledCallback = std::function<void(int)>;

    SegmentedItem(std::string_view label, std::vector<std::string_view> options, int selectedIndex = 0,
                  bool wraps = false, ChangedCallback onChanged = {}, CommittedCallback onCommitted = {},
                  CancelledCallback onCancelled = {});

    ItemActivation activate() override;
    int preferredHeight() const override;
    bool adjust(int direction) override;
    void beginEdit() override;
    void commitEdit() override;
    void cancelEdit() override;
    bool update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const override;

    int selectedIndex() const
    {
        return _selected_index;
    }

    std::string_view selectedValue() const;
    void setSelectedIndex(int index, bool animated = true);

private:
    int normalizeIndex(int index) const;
    bool setSelectedIndexInternal(int index, bool animated, bool notifyChanged);

    std::vector<std::string_view> _options;
    int _selected_index = 0;
    int _snapshot_index = 0;
    bool _wraps         = false;
    ChangedCallback _on_changed;
    CommittedCallback _on_committed;
    CancelledCallback _on_cancelled;
    uitk::AnimateValue _indicator_position;
    float _last_indicator_position = 0.0F;
};

}  // namespace settings::view
