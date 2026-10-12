/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "roller_item.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <utility>

namespace {

constexpr int PreferredHeight       = 104;
constexpr int HorizontalInset       = 14;
constexpr int LabelTop              = 10;
constexpr int WheelCenterOffset     = 68;
constexpr int RowPitch              = 24;
constexpr int SelectionHeight       = 28;
constexpr int SelectionRadius       = 8;
constexpr int WheelTop              = 34;
constexpr int WheelBottomInset      = 2;
constexpr int WheelFadeHeight       = 12;
constexpr float PositionDuration    = 0.3F;
constexpr float PositionBounce      = 0.1F;
constexpr float AnimationDifference = 0.0005F;

const lgfx::LVGLFont LabelFont{&font_noto_sans_semibold_20, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont OptionFont{&font_noto_sans_semibold_20, lgfx::lvgl_font_blend_t::destination};

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

void configureSpring(uitk::AnimateValue& animation, float visualDuration, float bounce)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = visualDuration;
    options.bounce         = bounce;
    options.restDelta      = 0.01F;
    options.restSpeed      = 0.01F;
    animation.delay        = 0.0F;
}

}  // namespace

namespace settings::view {

RollerItem::RollerItem(std::string_view label, std::vector<std::string_view> options, int selectedIndex, bool wraps,
                       ChangedCallback onChanged, CommittedCallback onCommitted, CancelledCallback onCancelled)
    : MenuItem(label),
      _options(std::move(options)),
      _wraps(wraps),
      _on_changed(std::move(onChanged)),
      _on_committed(std::move(onCommitted)),
      _on_cancelled(std::move(onCancelled))
{
    if (!_options.empty()) {
        _selected_index = std::clamp(selectedIndex, 0, static_cast<int>(_options.size()) - 1);
        _logical_index  = _selected_index;
    }
    _logical_snapshot = _logical_index;

    configureSpring(_position, PositionDuration, PositionBounce);
    _position.teleport(static_cast<float>(_logical_index));
    _last_position = _position.directValue();
}

ItemActivation RollerItem::activate()
{
    return _options.empty() ? ItemActivation::Handled : ItemActivation::BeginEdit;
}

int RollerItem::preferredHeight() const
{
    return PreferredHeight;
}

bool RollerItem::adjust(int direction)
{
    if (_options.empty() || direction == 0) {
        return false;
    }
    return setLogicalIndex(_logical_index + (direction > 0 ? 1 : -1), true, true);
}

void RollerItem::beginEdit()
{
    _logical_snapshot = _logical_index;
}

void RollerItem::commitEdit()
{
    if (_on_committed && !_on_committed(_selected_index)) {
        const int previous_index = _selected_index;
        setLogicalIndex(_logical_snapshot, true, false);
        if (previous_index != _selected_index && _on_cancelled) {
            _on_cancelled(_selected_index);
        }
    }
}

void RollerItem::cancelEdit()
{
    const int previous_index = _selected_index;
    setLogicalIndex(_logical_snapshot, true, false);
    if (previous_index != _selected_index && _on_cancelled) {
        _on_cancelled(_selected_index);
    }
}

bool RollerItem::update(std::uint32_t currentTime)
{
    const float current_time = static_cast<float>(currentTime) / 1000.0F;
    _position.update(current_time);

    const float position = _position.directValue();
    const bool changed   = std::abs(position - _last_position) > AnimationDifference;
    _last_position       = position;
    return changed;
}

void RollerItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    const auto& frame                  = state.frame;
    const float edit_mix               = std::clamp(state.editMix, 0.0F, 1.0F);
    const std::uint32_t surface_color  = blendColor(state.theme.surface, state.theme.surfaceEditing, edit_mix);
    const std::uint32_t text_color     = blendColor(state.theme.text, state.theme.textEditing, edit_mix);
    const std::uint32_t band_color     = blendColor(state.theme.track, state.theme.trackEditing, edit_mix);
    const std::uint32_t adjacent_edit  = blendColor(state.theme.surfaceEditing, state.theme.textEditing, 0.48F);
    const std::uint32_t adjacent_color = blendColor(state.theme.surface, adjacent_edit, edit_mix);

    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, 16, surface_color);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setTextColor(text_color);
    canvas.setBaseColor(surface_color);
    canvas.setFont(&LabelFont);
    canvas.drawString(label().data(), frame.x + HorizontalInset, frame.y + LabelTop);

    const int selection_x     = frame.x + HorizontalInset;
    const int selection_y     = frame.y + WheelCenterOffset - SelectionHeight / 2;
    const int selection_width = frame.width - HorizontalInset * 2;
    canvas.fillSmoothRoundRect(selection_x, selection_y, selection_width, SelectionHeight, SelectionRadius, band_color);

    if (_options.empty()) {
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        canvas.setTextColor(text_color);
        canvas.setFont(&OptionFont);
        canvas.drawString("--", frame.x + frame.width / 2, frame.y + WheelCenterOffset);
        return;
    }

    const float position     = _last_position;
    const int anchor         = static_cast<int>(std::floor(position));
    const int center_x       = frame.x + frame.width / 2;
    const int wheel_center_y = frame.y + WheelCenterOffset;
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setFont(&OptionFont);

    const int wheel_top    = frame.y + WheelTop;
    const int wheel_bottom = frame.y + frame.height - WheelBottomInset;
    canvas.setClipRect(frame.x, wheel_top, frame.width, std::max(0, wheel_bottom - wheel_top));

    for (int logical_index = anchor - 2; logical_index <= anchor + 2; ++logical_index) {
        if (!_wraps && (logical_index < 0 || logical_index >= static_cast<int>(_options.size()))) {
            continue;
        }

        const float distance = static_cast<float>(logical_index) - position;
        const int row_y      = wheel_center_y + roundToInt(distance * static_cast<float>(RowPitch));
        if (row_y < wheel_top || row_y > wheel_bottom) {
            continue;
        }

        const float proximity = std::clamp(1.0F - std::abs(distance), 0.0F, 1.0F);
        canvas.setTextColor(blendColor(adjacent_color, text_color, proximity));
        canvas.setBaseColor(proximity >= 0.5F ? band_color : surface_color);
        canvas.drawString(_options[static_cast<std::size_t>(normalizeIndex(logical_index))].data(), center_x, row_y);
    }

    canvas.setClipRect(0, 0, canvas.width(), canvas.height());

    const int fade_height = std::min(WheelFadeHeight, std::max(0, (wheel_bottom - wheel_top) / 2));
    const int fade_x      = selection_x;
    const int fade_width  = selection_width;
    for (int offset = 0; offset < fade_height; ++offset) {
        const float edge_mix = static_cast<float>(fade_height - offset) / static_cast<float>(fade_height);
        const auto alpha     = static_cast<std::uint8_t>(std::lround(edge_mix * 220.0F));
        if (alpha == 0U) {
            continue;
        }
        canvas.fillRectAlpha(fade_x, wheel_top + offset, fade_width, 1, alpha, surface_color);
        canvas.fillRectAlpha(fade_x, wheel_bottom - 1 - offset, fade_width, 1, alpha, surface_color);
    }
}

std::string_view RollerItem::selectedValue() const
{
    if (_options.empty()) {
        return {};
    }
    return _options[static_cast<std::size_t>(_selected_index)];
}

void RollerItem::setSelectedIndex(int index, bool animated)
{
    if (_options.empty()) {
        return;
    }

    const int target_index = std::clamp(index, 0, static_cast<int>(_options.size()) - 1);
    if (!_wraps) {
        setLogicalIndex(target_index, animated, true);
        return;
    }

    int delta       = target_index - normalizeIndex(_logical_index);
    const int count = static_cast<int>(_options.size());
    if (delta > count / 2) {
        delta -= count;
    } else if (delta < -count / 2) {
        delta += count;
    }
    setLogicalIndex(_logical_index + delta, animated, true);
}

int RollerItem::normalizeIndex(int index) const
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

bool RollerItem::setLogicalIndex(int logicalIndex, bool animated, bool notifyChanged)
{
    if (_options.empty()) {
        return false;
    }

    if (!_wraps) {
        logicalIndex = std::clamp(logicalIndex, 0, static_cast<int>(_options.size()) - 1);
    }
    if (logicalIndex == _logical_index) {
        return false;
    }

    _logical_index  = logicalIndex;
    _selected_index = normalizeIndex(_logical_index);
    if (animated) {
        _position = static_cast<float>(_logical_index);
    } else {
        _position.teleport(static_cast<float>(_logical_index));
        _last_position = _position.directValue();
    }

    if (notifyChanged && _on_changed) {
        _on_changed(_selected_index);
    }
    return true;
}

}  // namespace settings::view
