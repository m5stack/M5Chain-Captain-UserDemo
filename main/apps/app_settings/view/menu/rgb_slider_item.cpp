/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rgb_slider_item.h"

#include <M5GFX.h>
#include <algorithm>
#include <array>
#include <assets/assets.h>
#include <cmath>
#include <hal/hal.h>
#include <utility>

namespace {

constexpr int PreferredHeight       = 92;
constexpr int CursorWidth           = 18;
constexpr std::uint32_t BorderColor = 0xBDBDBD;
constexpr std::uint32_t CursorColor = 0xFFFFFF;
constexpr float ColorFadeDuration   = 0.24F;
constexpr float PositionDuration    = 0.24F;
constexpr float PositionBounce      = 0.10F;
constexpr float AnimationDifference = 0.0005F;

constexpr std::array<std::uint32_t, settings::view::RgbSliderItem::PresetCount> PresetColors{
    0x000000,  // Off
    0xFF0000,  // Red
    0xFF5000,  // Orange
    0xFFFF00,  // Yellow
    0x00FF00,  // Green
    0x00FFFF,  // Cyan
    0x0000FF,  // Blue
    0x8000FF,  // Violet
    0xFF0080,  // Pink
    0xFFB060,  // Warm white
    0xFFFFFF,  // White
};

const lgfx::LVGLFont LabelFont{&font_noto_sans_semibold_14};

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

    const auto red   = blend_channel((from >> 16U) & 0xFFU, (to >> 16U) & 0xFFU);
    const auto green = blend_channel((from >> 8U) & 0xFFU, (to >> 8U) & 0xFFU);
    const auto blue  = blend_channel(from & 0xFFU, to & 0xFFU);
    return (red << 16U) | (green << 8U) | blue;
}

void drawColorStrip(lgfx::LGFX_Sprite& canvas, int x, int y, int width, int height)
{
    constexpr float Radius   = 6.0F;
    constexpr int LastPreset = settings::view::RgbSliderItem::PresetCount - 1;

    for (int offset = 0; offset < width; ++offset) {
        const float edge     = std::min(static_cast<float>(offset) + 0.5F, static_cast<float>(width - offset) - 0.5F);
        const float dx       = std::max(0.0F, Radius - edge);
        const float top      = Radius - std::sqrt(Radius * Radius - dx * dx);
        const int inset      = static_cast<int>(std::ceil(top));
        const float position = std::clamp(
            static_cast<float>(offset - CursorWidth / 2) / static_cast<float>(width - CursorWidth), 0.0F, 1.0F);
        const int index  = std::clamp(static_cast<int>(std::lround(position * LastPreset)), 0, LastPreset);
        const auto color = PresetColors[static_cast<std::size_t>(index)];
        canvas.drawFastVLine(x + offset, y + inset, height - inset * 2, color);
        const auto alpha = static_cast<std::uint8_t>(std::lround((static_cast<float>(inset) - top) * 255.0F));
        if (inset > 0 && alpha > 0) {
            canvas.fillRectAlpha(x + offset, y + inset - 1, 1, 1, alpha, color);
            canvas.fillRectAlpha(x + offset, y + height - inset, 1, 1, alpha, color);
        }
    }
}

std::uint32_t colorAtPosition(float position)
{
    const int index =
        std::clamp(roundToInt(position * static_cast<float>(settings::view::RgbSliderItem::PresetCount - 1)), 0,
                   settings::view::RgbSliderItem::PresetCount - 1);
    return PresetColors[static_cast<std::size_t>(index)];
}

}  // namespace

namespace settings::view {

RgbSliderItem::RgbSliderItem(int index, ChangedCallback onChanged, CancelledCallback onCancelled)
    : MenuItem("RGB"),
      _index(clampIndex(index)),
      _snapshot_index(_index),
      _on_changed(std::move(onChanged)),
      _on_cancelled(std::move(onCancelled))
{
    auto& options          = _color_mix.easingOptions();
    options.duration       = ColorFadeDuration;
    options.easingFunction = uitk::ease::ease_out_quad;
    _color_mix.delay       = 0.0F;
    _color_mix.teleport(1.0F);
    _last_color_mix = 1.0F;

    auto& position_options          = _position.springOptions();
    position_options.duration       = 0.0F;
    position_options.visualDuration = PositionDuration;
    position_options.bounce         = PositionBounce;
    position_options.restDelta      = 0.001F;
    position_options.restSpeed      = 0.001F;
    _position.teleport(static_cast<float>(_index));
    _last_position = static_cast<float>(_index);
}

ItemActivation RgbSliderItem::activate()
{
    return ItemActivation::BeginEdit;
}

int RgbSliderItem::preferredHeight() const
{
    return PreferredHeight;
}

bool RgbSliderItem::adjust(int direction)
{
    if (direction == 0) return false;

    auto& hal          = GetHAL();
    const bool held    = direction < 0 ? hal.btnB.isPressed() : hal.btnC.isPressed();
    const int last     = PresetCount - 1;
    const bool at_edge = direction < 0 ? _index == 0 : _index == last;
    if (held && at_edge && _last_adjust_direction == direction) {
        return false;
    }

    const int next         = direction > 0 ? (_index + 1) % PresetCount : (_index + PresetCount - 1) % PresetCount;
    _last_adjust_direction = direction;
    const bool wrapped     = std::abs(next - _index) > 1;
    const bool changed     = setIndex(next);
    if (changed && wrapped) {
        _position.teleport(static_cast<float>(next));
    }
    return changed;
}

void RgbSliderItem::beginEdit()
{
    _snapshot_index        = _index;
    _last_adjust_direction = 0;
    _color_mix.teleport(0.0F);
    _color_mix      = 1.0F;
    _last_color_mix = 0.0F;
    _dirty          = true;
}

void RgbSliderItem::commitEdit()
{
}

void RgbSliderItem::cancelEdit()
{
    if (_index == _snapshot_index) return;
    _index    = _snapshot_index;
    _position = static_cast<float>(_index);
    _dirty    = true;
    if (_on_cancelled) _on_cancelled(_index);
}

bool RgbSliderItem::update(std::uint32_t currentTime)
{
    const float current_time = static_cast<float>(currentTime) / 1000.0F;
    _color_mix.update(current_time);
    _position.update(current_time);
    if (_last_adjust_direction != 0 && !GetHAL().btnB.isPressed() && !GetHAL().btnC.isPressed()) {
        _last_adjust_direction = 0;
    }

    const float color_mix = _color_mix.directValue();
    const float position  = _position.directValue();
    const bool dirty      = _dirty || std::abs(color_mix - _last_color_mix) > AnimationDifference ||
                       std::abs(position - _last_position) > AnimationDifference;
    _last_color_mix = color_mix;
    _last_position  = position;
    _dirty          = false;
    return dirty;
}

void RgbSliderItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    constexpr int PanelRadius = 12;
    const float edit_mix      = std::clamp(state.editMix, 0.0F, 1.0F);
    const auto panel_color    = blendColor(state.theme.surface, state.theme.surfaceEditing, edit_mix);
    const auto text_color     = blendColor(state.theme.text, state.theme.textEditing, edit_mix);
    const auto color          = presetColor(_index);

    canvas.setBaseColor(state.theme.background);
    canvas.fillSmoothRoundRect(state.frame.x, state.frame.y, state.frame.width, state.frame.height, PanelRadius,
                               panel_color);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setFont(&LabelFont);
    canvas.setTextColor(text_color);
    canvas.setBaseColor(panel_color);
    canvas.drawString("RGB", state.frame.x + 13, state.frame.y + 7);

    const int strip_x      = state.frame.x + 12;
    const int strip_y      = state.frame.y + 34;
    const int strip_width  = state.frame.width - 24;
    const int strip_height = 44;
    canvas.fillSmoothRoundRect(strip_x - 2, strip_y - 2, strip_width + 4, strip_height + 4, 8, BorderColor);
    drawColorStrip(canvas, strip_x, strip_y, strip_width, strip_height);

    if (state.editing) {
        const float position = std::clamp(_last_position / static_cast<float>(PresetCount - 1), 0.0F, 1.0F);
        const int cursor_x   = strip_x + roundToInt(position * static_cast<float>(strip_width - CursorWidth));
        if (_last_color_mix > AnimationDifference) {
            const auto base_color = colorAtPosition(position);
            canvas.fillSmoothRoundRect(cursor_x, strip_y - 4, CursorWidth, strip_height + 8, 6,
                                       blendColor(base_color, CursorColor, _last_color_mix));
            canvas.fillSmoothRoundRect(cursor_x + 3, strip_y - 1, CursorWidth - 6, strip_height + 2, 3,
                                       blendColor(base_color, color, _last_color_mix));
        }
    }
}

bool RgbSliderItem::setIndex(int index, bool notifyChanged)
{
    const int next = clampIndex(index);
    if (next == _index) return false;
    _index    = next;
    _position = static_cast<float>(_index);
    _dirty    = true;
    if (notifyChanged && _on_changed) _on_changed(_index);
    return true;
}

std::uint32_t RgbSliderItem::presetColor(int index)
{
    return PresetColors[static_cast<std::size_t>(clampIndex(index))];
}

int RgbSliderItem::clampIndex(int index)
{
    return std::clamp(index, 0, PresetCount - 1);
}

}  // namespace settings::view
