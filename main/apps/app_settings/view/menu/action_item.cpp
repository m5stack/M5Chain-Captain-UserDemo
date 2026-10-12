/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "action_item.h"

#include <M5GFX.h>
#include <assets/assets.h>
#include <utility>

namespace {

constexpr int PreferredHeight = 64;
constexpr int Radius          = 16;
constexpr int HorizontalInset = 14;

const lgfx::LVGLFont LabelFont24{&font_noto_sans_medium_24};
const lgfx::LVGLFont LabelFont20{&font_noto_sans_semibold_20};
const lgfx::LVGLFont LabelFont16{&font_noto_sans_semibold_16};
const lgfx::LVGLFont LabelFont14{&font_noto_sans_semibold_14};

const lgfx::LVGLFont* labelFontFor(std::uint8_t size)
{
    switch (size) {
        case 14:
            return &LabelFont14;
        case 16:
            return &LabelFont16;
        case 20:
            return &LabelFont20;
        case 24:
        default:
            return &LabelFont24;
    }
}

}  // namespace

namespace settings::view {

ActionItem::ActionItem(std::string_view label, Callback onActivated)
    : MenuItem(label), _on_activated(std::move(onActivated))
{
}

void ActionItem::setEnabled(bool enabled)
{
    if (_enabled == enabled) return;
    _enabled = enabled;
    _dirty   = true;
}

ItemActivation ActionItem::activate()
{
    if (_enabled && _on_activated) {
        _on_activated();
    }
    return ItemActivation::Handled;
}

int ActionItem::preferredHeight() const
{
    return PreferredHeight;
}

bool ActionItem::update(std::uint32_t currentTime)
{
    (void)currentTime;
    return std::exchange(_dirty, false);
}

void ActionItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    const auto& frame  = state.frame;
    const auto surface = _enabled ? state.theme.surface : state.theme.disabledSurface;
    const auto text    = _enabled ? state.theme.text : state.theme.disabledText;
    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, Radius, surface);

    const int label_width = frame.width - HorizontalInset * 2;
    if (label_width > 0) {
        if (_resolved_label_width != label_width) {
            _resolved_label =
                resolveLabel(canvas, label(), label_width,
                             {{24, &LabelFont24}, {20, &LabelFont20}, {16, &LabelFont16}, {14, &LabelFont14}});
            _resolved_label_width = label_width;
        }
        const auto* font = labelFontFor(_resolved_label.fontSize);
        canvas.setClipRect(frame.x + HorizontalInset, frame.y, label_width, frame.height);
        canvas.setTextSize(1);
        canvas.setTextColor(text);
        canvas.setBaseColor(surface);
        canvas.setFont(font);
        const int center_x = frame.x + frame.width / 2;
        if (_resolved_label.lineCount <= 1) {
            canvas.setTextDatum(lgfx::textdatum_t::middle_center);
            canvas.drawString(_resolved_label.lines[0].c_str(), center_x, frame.y + frame.height / 2);
        } else {
            constexpr int LineGap = 0;
            const int line_height = canvas.fontHeight(font);
            const int text_height = line_height * 2 + LineGap;
            const int text_y      = frame.y + (frame.height - text_height) / 2;
            canvas.setTextDatum(lgfx::textdatum_t::top_center);
            canvas.drawString(_resolved_label.lines[0].c_str(), center_x, text_y);
            canvas.drawString(_resolved_label.lines[1].c_str(), center_x, text_y + line_height + LineGap);
        }
        canvas.setClipRect(0, 0, canvas.width(), canvas.height());
    }
}

}  // namespace settings::view
