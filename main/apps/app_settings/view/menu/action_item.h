/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "label_resolver.h"
#include "menu_item.h"

#include <functional>

namespace settings::view {

class ActionItem final : public MenuItem {
public:
    using Callback = std::function<void()>;

    explicit ActionItem(std::string_view label, Callback onActivated = {});

    bool enabled() const override
    {
        return _enabled;
    }
    void setEnabled(bool enabled);
    ItemActivation activate() override;
    int preferredHeight() const override;
    bool update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const override;

private:
    Callback _on_activated;
    bool _enabled                     = true;
    bool _dirty                       = false;
    mutable int _resolved_label_width = -1;
    mutable ResolvedLabel _resolved_label;
};

}  // namespace settings::view
