/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "segmented_item.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <utility>

namespace {

constexpr int PreferredHeight       = 86;
constexpr int HorizontalInset       = 14;
constexpr int LabelTop              = 10;
constexpr int SegmentTop            = 41;
constexpr int SegmentHeight         = 30;
constexpr int TrackRadius           = 9;
constexpr int IndicatorInset        = 3;
constexpr int IndicatorRadius       = 6;
constexpr float IndicatorDuration   = 0.32F;
constexpr float IndicatorBounce     = 0.14F;
constexpr float AnimationDifference = 0.0005F;

const lgfx::LVGLFont LabelFont{&font_noto_sans_semibold_20, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont SegmentFont{&font_noto_sans_semibold_16, lgfx::lvgl_font_blend_t::destination};

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

void configureSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = IndicatorDuration;
    options.bounce         = IndicatorBounce;
    options.restDelta      = 0.01F;
    options.restSpeed      = 0.01F;
    animation.delay        = 0.0F;
}

}  // namespace

namespace settings::view {

SegmentedItem::SegmentedItem(std::string_view label, std::vector<std::string_view> options, int selectedIndex,
                             bool wraps, ChangedCallback onChanged, CommittedCallback onCommitted,
                             CancelledCallback onCancelled)
    : MenuItem(label),
      _options(std::move(options)),
      _wraps(wraps),
      _on_changed(std::move(onChanged)),
      _on_committed(std::move(onCommitted)),
      _on_cancelled(std::move(onCancelled))
{
    if (!_options.empty()) {
        _selected_index = std::clamp(selectedIndex, 0, static_cast<int>(_options.size()) - 1);
    }
    _snapshot_index = _selected_index;

    configureSpring(_indicator_position);
    _indicator_position.teleport(static_cast<float>(_selected_index));
    _last_indicator_position = _indicator_position.directValue();
}

ItemActivation SegmentedItem::activate()
{
    return _options.empty() ? ItemActivation::Handled : ItemActivation::BeginEdit;
}

int SegmentedItem::preferredHeight() const
{
    return PreferredHeight;
}

bool SegmentedItem::adjust(int direction)
{
    if (_options.empty() || direction == 0) {
        return false;
    }

    int next_index = _selected_index + (direction > 0 ? 1 : -1);
    if (_wraps) {
        next_index = normalizeIndex(next_index);
    } else {
        next_index = std::clamp(next_index, 0, static_cast<int>(_options.size()) - 1);
    }
    if (next_index == _selected_index) {
        return false;
    }

    setSelectedIndexInternal(next_index, true, true);
    return true;
}

void SegmentedItem::beginEdit()
{
    _snapshot_index = _selected_index;
}

void SegmentedItem::commitEdit()
{
    if (_on_committed && !_on_committed(_selected_index)) {
        const int previous_index = _selected_index;
        setSelectedIndexInternal(_snapshot_index, true, false);
        if (previous_index != _selected_index && _on_cancelled) {
            _on_cancelled(_selected_index);
        }
    }
}

void SegmentedItem::cancelEdit()
{
    const int previous_index = _selected_index;
    setSelectedIndexInternal(_snapshot_index, true, false);
    if (previous_index != _selected_index && _on_cancelled) {
        _on_cancelled(_selected_index);
    }
}

bool SegmentedItem::update(std::uint32_t currentTime)
{
    _indicator_position.update(static_cast<float>(currentTime) / 1000.0F);
    const float position     = _indicator_position.directValue();
    const bool changed       = std::abs(position - _last_indicator_position) > AnimationDifference;
    _last_indicator_position = position;
    return changed;
}

void SegmentedItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    const auto& frame                    = state.frame;
    const float edit_mix                 = std::clamp(state.editMix, 0.0F, 1.0F);
    const std::uint32_t surface_color    = blendColor(state.theme.surface, state.theme.surfaceEditing, edit_mix);
    const std::uint32_t label_color      = blendColor(state.theme.text, state.theme.textEditing, edit_mix);
    const std::uint32_t track_color      = blendColor(state.theme.track, state.theme.trackEditing, edit_mix);
    const std::uint32_t indicator_color  = blendColor(state.theme.accent, state.theme.accentEditing, edit_mix);
    const std::uint32_t unselected_edit  = blendColor(state.theme.surfaceEditing, state.theme.textEditing, 0.7F);
    const std::uint32_t unselected_color = blendColor(state.theme.secondaryText, unselected_edit, edit_mix);
    const std::uint32_t selected_color   = blendColor(state.theme.controlHighlight, state.theme.text, edit_mix);

    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, 16, surface_color);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setTextColor(label_color);
    canvas.setBaseColor(surface_color);
    canvas.setFont(&LabelFont);
    canvas.drawString(label().data(), frame.x + HorizontalInset, frame.y + LabelTop);

    if (_options.empty()) {
        return;
    }

    const int track_x         = frame.x + HorizontalInset;
    const int track_y         = frame.y + SegmentTop;
    const int track_width     = frame.width - HorizontalInset * 2;
    const float segment_width = static_cast<float>(track_width) / static_cast<float>(_options.size());
    canvas.fillSmoothRoundRect(track_x, track_y, track_width, SegmentHeight, TrackRadius, track_color);

    const float visual_position =
        std::clamp(_last_indicator_position, -0.08F, static_cast<float>(_options.size() - 1) + 0.08F);
    const int indicator_x      = track_x + roundToInt(segment_width * visual_position) + IndicatorInset;
    const int indicator_width  = std::max(1, roundToInt(segment_width) - IndicatorInset * 2);
    const int indicator_height = SegmentHeight - IndicatorInset * 2;
    canvas.fillSmoothRoundRect(indicator_x, track_y + IndicatorInset, indicator_width, indicator_height,
                               IndicatorRadius, indicator_color);

    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setFont(&SegmentFont);
    for (std::size_t index = 0; index < _options.size(); ++index) {
        const float center_x = static_cast<float>(track_x) + segment_width * (static_cast<float>(index) + 0.5F);
        const float proximity =
            std::clamp(1.0F - std::abs(static_cast<float>(index) - _last_indicator_position), 0.0F, 1.0F);
        canvas.setTextColor(blendColor(unselected_color, selected_color, proximity));
        canvas.drawString(_options[index].data(), roundToInt(center_x), track_y + SegmentHeight / 2);
    }
}

std::string_view SegmentedItem::selectedValue() const
{
    if (_options.empty()) {
        return {};
    }
    return _options[static_cast<std::size_t>(_selected_index)];
}

void SegmentedItem::setSelectedIndex(int index, bool animated)
{
    setSelectedIndexInternal(index, animated, true);
}

bool SegmentedItem::setSelectedIndexInternal(int index, bool animated, bool notifyChanged)
{
    if (_options.empty()) {
        return false;
    }

    const int next_index = std::clamp(index, 0, static_cast<int>(_options.size()) - 1);
    if (next_index == _selected_index) {
        return false;
    }

    _selected_index = next_index;
    if (animated) {
        _indicator_position = static_cast<float>(_selected_index);
    } else {
        _indicator_position.teleport(static_cast<float>(_selected_index));
        _last_indicator_position = _indicator_position.directValue();
    }
    if (notifyChanged && _on_changed) {
        _on_changed(_selected_index);
    }
    return true;
}

int SegmentedItem::normalizeIndex(int index) const
{
    if (_options.empty()) {
        return 0;
    }
    const int count = static_cast<int>(_options.size());
    int normalized  = index % count;
    if (normalized < 0) {
        normalized += count;
    }
    return normalized;
}

}  // namespace settings::view
