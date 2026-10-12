/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "switch_item.h"

#include "label_resolver.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <utility>

namespace {

constexpr int PreferredHeight  = 64;
constexpr int HorizontalInset  = 14;
constexpr int TrackWidth       = 52;
constexpr int TrackHeight      = 32;
constexpr int KnobSize         = 28;
constexpr int KnobStretch      = 8;
constexpr int LabelTrackGap    = 10;
constexpr float ToggleDuration = 0.18F;

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
        default:
            return &LabelFont20;
    }
}

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

std::uint32_t blendColor(std::uint32_t from, std::uint32_t to, float mix)
{
    mix                      = std::clamp(mix, 0.0F, 1.0F);
    const auto blend_channel = [mix](int a, int b) {
        return static_cast<std::uint32_t>(std::lround(static_cast<float>(a) + static_cast<float>(b - a) * mix));
    };

    const auto red   = blend_channel((from >> 16) & 0xFF, (to >> 16) & 0xFF);
    const auto green = blend_channel((from >> 8) & 0xFF, (to >> 8) & 0xFF);
    const auto blue  = blend_channel(from & 0xFF, to & 0xFF);
    return (red << 16) | (green << 8) | blue;
}

}  // namespace

namespace settings::view {

SwitchItem::SwitchItem(std::string_view label, bool value, CommittedCallback onCommitted)
    : MenuItem(label), _value(value), _on_committed(std::move(onCommitted))
{
    auto& options          = _state_mix.easingOptions();
    options.duration       = ToggleDuration;
    options.easingFunction = uitk::ease::ease_out_cubic;
    _state_mix.delay       = 0.0F;
    _state_mix.teleport(_value ? 1.0F : 0.0F);
    _last_state_mix = _state_mix.directValue();
}

ItemActivation SwitchItem::activate()
{
    setValue(!_value);
    return ItemActivation::Handled;
}

int SwitchItem::preferredHeight() const
{
    return PreferredHeight;
}

bool SwitchItem::update(std::uint32_t currentTime)
{
    _state_mix.update(static_cast<float>(currentTime) / 1000.0F);
    const float state_mix = _state_mix.directValue();
    const bool changed    = std::abs(state_mix - _last_state_mix) > 0.0005F;
    _last_state_mix       = state_mix;
    return changed;
}

void SwitchItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    const auto& frame     = state.frame;
    const int track_x     = frame.x + frame.width - HorizontalInset - TrackWidth;
    const int track_y     = frame.y + (frame.height - TrackHeight) / 2;
    const int label_x     = frame.x + HorizontalInset;
    const int label_width = std::max(0, track_x - LabelTrackGap - label_x);
    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, 16, state.theme.surface);

    if (label_width > 0) {
        if (_resolved_label_width != label_width) {
            const auto resolved = resolveLabel(canvas, label(), label_width,
                                               {{20, &LabelFont20}, {16, &LabelFont16}, {14, &LabelFont14}});
            _label_font_size      = resolved.fontSize;
            _label_line_count     = resolved.lineCount;
            _label_lines          = resolved.lines;
            _resolved_label_width = label_width;
        }
        canvas.setClipRect(label_x, frame.y, label_width, frame.height);
        canvas.setTextSize(1);
        canvas.setTextDatum(lgfx::textdatum_t::middle_left);
        canvas.setTextColor(state.theme.text);
        canvas.setBaseColor(state.theme.surface);
        const auto* label_font = labelFontFor(_label_font_size);
        canvas.setFont(label_font);
        if (_label_line_count <= 1) {
            canvas.setTextDatum(lgfx::textdatum_t::middle_left);
            canvas.drawString(_label_lines[0].c_str(), label_x, frame.y + frame.height / 2);
        } else {
            constexpr int LineGap = 0;
            const int line_height = canvas.fontHeight(label_font);
            const int text_height = line_height * 2 + LineGap;
            const int text_y      = frame.y + (frame.height - text_height) / 2;
            canvas.setTextDatum(lgfx::textdatum_t::top_left);
            canvas.drawString(_label_lines[0].c_str(), label_x, text_y);
            canvas.drawString(_label_lines[1].c_str(), label_x, text_y + line_height + LineGap);
        }
        canvas.setClipRect(0, 0, canvas.width(), canvas.height());
    }

    const float state_mix     = std::clamp(_last_state_mix, 0.0F, 1.0F);
    const float motion_mix    = 4.0F * state_mix * (1.0F - state_mix);
    const int knob_travel     = TrackWidth - KnobSize - 4;
    const int knob_center_x   = track_x + 2 + KnobSize / 2 + roundToInt(static_cast<float>(knob_travel) * state_mix);
    const int knob_width      = KnobSize + roundToInt(static_cast<float>(KnobStretch) * motion_mix);
    const int knob_height     = KnobSize;
    const int knob_x          = knob_center_x - knob_width / 2;
    const int knob_y          = track_y + (TrackHeight - knob_height) / 2;
    const std::uint32_t color = blendColor(state.theme.track, state.theme.accent, state_mix);

    canvas.fillSmoothRoundRect(track_x, track_y, TrackWidth, TrackHeight, TrackHeight / 2, color);
    canvas.fillSmoothRoundRect(knob_x, knob_y, knob_width, knob_height, knob_height / 2, state.theme.controlHighlight);
}

void SwitchItem::setValue(bool value, bool animated)
{
    if (value == _value) {
        return;
    }

    const bool previous_value = _value;
    _value                    = value;
    if (animated) {
        _state_mix = _value ? 1.0F : 0.0F;
    } else {
        _state_mix.teleport(_value ? 1.0F : 0.0F);
    }

    if (_on_committed && !_on_committed(_value)) {
        _value = previous_value;
        if (animated) {
            _state_mix = _value ? 1.0F : 0.0F;
        } else {
            _state_mix.teleport(_value ? 1.0F : 0.0F);
        }
    }
}

}  // namespace settings::view
