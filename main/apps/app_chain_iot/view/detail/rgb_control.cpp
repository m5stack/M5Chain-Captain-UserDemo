/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rgb_control.h"

#include "../menu/node_draw.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/audio/audio.h>
#include <array>
#include <assets/assets.h>
#include <cmath>
#include <limits>

namespace {

using Palette = chain_iot::view::RgbControl::Palette;

constexpr int CursorWidth           = 18;
constexpr std::uint8_t Brightness   = 75;
constexpr std::uint32_t TextColor   = 0x6F6B7C;
constexpr std::uint32_t HintColor   = 0xBDBDBD;
constexpr std::uint32_t BorderColor = 0xBDBDBD;
constexpr std::uint32_t CursorColor = 0xFFFFFF;
constexpr float AnimationDifference = 0.0005F;

constexpr std::array<std::uint32_t, 11> PresetColors{
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
constexpr int LastPreset          = static_cast<int>(PresetColors.size()) - 1;
constexpr int LastGrayscalePreset = 8;

const lgfx::LVGLFont LabelFont{&font_noto_sans_semibold_14};

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

std::uint32_t packedColor(RgbColor color)
{
    return (static_cast<std::uint32_t>(color.red) << 16U) | (static_cast<std::uint32_t>(color.green) << 8U) |
           static_cast<std::uint32_t>(color.blue);
}

int lastPreset(Palette palette)
{
    return palette == Palette::Grayscale ? LastGrayscalePreset : LastPreset;
}

std::uint32_t rawPresetColor(int index, Palette palette)
{
    if (palette == Palette::Grayscale) {
        const auto level = static_cast<std::uint32_t>((index * 255 + LastGrayscalePreset / 2) / LastGrayscalePreset);
        return (level << 16U) | (level << 8U) | level;
    }
    return PresetColors[index];
}

RgbColor presetColor(int index, Palette palette)
{
    const auto color   = rawPresetColor(index, palette);
    const auto channel = [palette](std::uint32_t value) {
        if (palette == Palette::Grayscale) return static_cast<std::uint8_t>(value & 0xFFU);
        return static_cast<std::uint8_t>(((value & 0xFFU) * Brightness + 50U) / 100U);
    };
    return {channel(color >> 16U), channel(color >> 8U), channel(color)};
}

int nearestPreset(RgbColor color, Palette palette)
{
    int nearest  = 0;
    int distance = std::numeric_limits<int>::max();
    for (int index = 0; index <= lastPreset(palette); ++index) {
        const auto preset = presetColor(index, palette);
        const int red     = static_cast<int>(color.red) - preset.red;
        const int green   = static_cast<int>(color.green) - preset.green;
        const int blue    = static_cast<int>(color.blue) - preset.blue;
        const int next    = red * red + green * green + blue * blue;
        if (next < distance) {
            distance = next;
            nearest  = index;
        }
    }
    return nearest;
}

std::uint32_t blendColor(std::uint32_t from, std::uint32_t to, float mix)
{
    const auto alpha = static_cast<std::uint8_t>(roundToInt(std::clamp(mix, 0.0F, 1.0F) * 255.0F));
    return lv_color_to_u32(lv_color_mix(lv_color_hex(to), lv_color_hex(from), alpha));
}

int interpolate(int from, int to, float mix)
{
    return roundToInt(static_cast<float>(from) + static_cast<float>(to - from) * mix);
}

void drawColorStrip(lgfx::LGFX_Sprite& canvas, const chain_iot::view::Frame& frame, std::uint32_t color, float mix,
                    Palette palette)
{
    constexpr float Radius = 6.0F;
    const int last_preset  = lastPreset(palette);
    for (int x = 0; x < frame.width; ++x) {
        const float edge     = std::min(static_cast<float>(x) + 0.5F, static_cast<float>(frame.width - x) - 0.5F);
        const float dx       = std::max(0.0F, Radius - edge);
        const float top      = Radius - std::sqrt(Radius * Radius - dx * dx);
        const int inset      = static_cast<int>(std::ceil(top));
        const float position = std::clamp(
            static_cast<float>(x - CursorWidth / 2) / static_cast<float>(frame.width - CursorWidth), 0.0F, 1.0F);
        const float stop = position * last_preset;
        const int index  = static_cast<int>(stop);
        const auto strip_color =
            blendColor(rawPresetColor(index, palette), rawPresetColor(std::min(index + 1, last_preset), palette),
                       stop - static_cast<float>(index));
        const auto column_color = blendColor(color, strip_color, mix);
        canvas.drawFastVLine(frame.x + x, frame.y + inset, frame.height - inset * 2, column_color);
        const auto alpha = static_cast<std::uint8_t>(roundToInt((static_cast<float>(inset) - top) * 255.0F));
        if (inset > 0 && alpha > 0) {
            canvas.fillRectAlpha(frame.x + x, frame.y + inset - 1, 1, 1, alpha, column_color);
            canvas.fillRectAlpha(frame.x + x, frame.y + frame.height - inset, 1, 1, alpha, column_color);
        }
    }
}

}  // namespace

namespace chain_iot::view {

RgbControl::RgbControl(Palette palette) : _palette(palette)
{
    auto& edit          = _edit_mix.springOptions();
    edit.duration       = 0.0F;
    edit.visualDuration = 0.38F;
    edit.bounce         = 0.16F;
    edit.restDelta      = 0.001F;
    edit.restSpeed      = 0.001F;
    _edit_mix.teleport(0.0F);

    auto& position          = _position.springOptions();
    position.duration       = 0.0F;
    position.visualDuration = 0.24F;
    position.bounce         = 0.10F;
    position.restDelta      = 0.001F;
    position.restSpeed      = 0.001F;
    _position.teleport(0.0F);
}

bool RgbControl::setColor(RgbColor color)
{
    if (_editing || packedColor(color) == packedColor(_color)) return false;
    _color          = color;
    _selected_index = nearestPreset(color, _palette);
    _dirty          = true;
    return true;
}

RgbColor RgbControl::color() const
{
    return _color;
}

void RgbControl::beginEdit()
{
    _snapshot            = _color;
    _snapshot_index      = _selected_index;
    _held_edge_direction = 0;
    _position.teleport(static_cast<float>(_selected_index));
    _rendered_position = static_cast<float>(_selected_index);
    _editing           = true;
    _edit_mix          = 1.0F;
    _dirty             = true;
}

void RgbControl::finishEdit(bool cancel)
{
    if (cancel) {
        _color          = _snapshot;
        _selected_index = _snapshot_index;
        _position       = static_cast<float>(_selected_index);
    }
    _editing             = false;
    _ok_armed            = false;
    _held_edge_direction = 0;
    _edit_mix            = 0.0F;
    _dirty               = true;
}

void RgbControl::adjust(int direction)
{
    const int last_preset = lastPreset(_palette);
    const int next_index  = _selected_index + direction;
    _selected_index       = next_index < 0 ? last_preset : (next_index > last_preset ? 0 : next_index);
    if (next_index != _selected_index)
        _position.teleport(static_cast<float>(_selected_index));
    else
        _position = static_cast<float>(_selected_index);
    _color = presetColor(_selected_index, _palette);
    _dirty = true;
}

bool RgbControl::handleInput(input::KeyEvent event)
{
    if (_held_edge_direction != 0 && !GetHAL().btnB.isPressed() && !GetHAL().btnC.isPressed()) {
        _held_edge_direction = 0;
    }
    switch (event) {
        case input::KeyEvent::OkPressed:
            _ok_armed = true;
            return true;
        case input::KeyEvent::OkReleased:
            if (!_ok_armed) return false;
            _ok_armed = false;
            if (_editing)
                finishEdit(false);
            else
                beginEdit();
            audio::play_ui_sfx(audio::UiSfx::Confirm);
            return true;
        case input::KeyEvent::Home:
            _ok_armed = false;
            if (!_editing) return false;
            finishEdit(true);
            audio::play_ui_sfx(audio::UiSfx::Back);
            return true;
        case input::KeyEvent::Previous:
        case input::KeyEvent::PreviousRepeat:
        case input::KeyEvent::Next:
        case input::KeyEvent::NextRepeat: {
            if (!_editing) return false;
            _ok_armed           = false;
            const bool repeated = event == input::KeyEvent::PreviousRepeat || event == input::KeyEvent::NextRepeat;
            const int direction =
                event == input::KeyEvent::Previous || event == input::KeyEvent::PreviousRepeat ? -1 : 1;
            const bool at_edge = direction < 0 ? _selected_index == 0 : _selected_index == lastPreset(_palette);
            if (repeated && (_held_edge_direction != 0 || at_edge)) {
                if (_held_edge_direction == 0) {
                    _held_edge_direction = direction;
                    audio::play_ui_sfx(audio::UiSfx::Boundary);
                }
                return true;
            }
            adjust(direction);
            audio::play_ui_sfx(audio::UiSfx::Navigate);
            return true;
        }
        default:
            return false;
    }
}

bool RgbControl::update(std::uint32_t currentTime)
{
    const float seconds = static_cast<float>(currentTime) / 1000.0F;
    _edit_mix.update(seconds);
    _position.update(seconds);
    const float mix      = std::round(_edit_mix.directValue() * 1000.0F) / 1000.0F;
    const float position = _position.directValue();
    const bool changed =
        _dirty || mix != _rendered_mix || std::abs(position - _rendered_position) > AnimationDifference;
    _rendered_mix      = mix;
    _rendered_position = position;
    _dirty             = false;
    return changed;
}

float RgbControl::expansion() const
{
    return std::clamp(_rendered_mix, -0.03F, 1.03F);
}

void RgbControl::draw(lgfx::LGFX_Sprite& canvas, const Frame& collapsed, const Frame& expanded) const
{
    const float mix         = expansion();
    const float opacity_mix = std::clamp(mix, 0.0F, 1.0F);
    const Frame frame{interpolate(collapsed.x, expanded.x, mix), interpolate(collapsed.y, expanded.y, mix),
                      interpolate(collapsed.width, expanded.width, mix),
                      interpolate(collapsed.height, expanded.height, mix)};
    canvas.setBaseColor(draw::InfoPanelColor);
    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, 12, draw::TileColor);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setFont(&LabelFont);
    canvas.setTextColor(TextColor);
    canvas.setBaseColor(draw::TileColor);
    canvas.drawString(_palette == Palette::Grayscale ? "Level" : "RGB", frame.x + 13, frame.y + 7);

    const auto hint_color = blendColor(HintColor, draw::TileColor, opacity_mix);
    canvas.setTextColor(hint_color);
    canvas.drawString("OK", frame.x + frame.width - 41, frame.y + 7);
    const int mark_x = frame.x + frame.width - 18;
    const int mark_y = frame.y + 10;
    canvas.fillSmoothRoundRect(mark_x, mark_y, 2, 6, 1, hint_color);
    canvas.fillSmoothRoundRect(mark_x + 7, mark_y + 4, 2, 6, 1, hint_color);
    canvas.fillSmoothRoundRect(mark_x, mark_y, 6, 2, 1, hint_color);
    canvas.fillSmoothRoundRect(mark_x + 3, mark_y + 8, 6, 2, 1, hint_color);

    const auto color = packedColor(_color);
    const Frame strip{frame.x + 12, frame.y + interpolate(30, 34, mix), frame.width - 24, interpolate(52, 44, mix)};
    canvas.fillSmoothRoundRect(strip.x - 2, strip.y - 2, strip.width + 4, strip.height + 4, 8, BorderColor);
    if (opacity_mix == 0.0F) {
        canvas.setBaseColor(BorderColor);
        canvas.fillSmoothRoundRect(strip.x, strip.y, strip.width, strip.height, 6, color);
        return;
    }
    drawColorStrip(canvas, strip, color, opacity_mix, _palette);

    if (_editing) {
        const float position = std::clamp(_rendered_position / static_cast<float>(lastPreset(_palette)), 0.0F, 1.0F);
        const int cursor_x   = strip.x + roundToInt(position * (strip.width - CursorWidth));
        canvas.fillSmoothRoundRect(cursor_x, strip.y - 4, CursorWidth, strip.height + 8, 6, CursorColor);
        canvas.fillSmoothRoundRect(cursor_x + 3, strip.y - 1, CursorWidth - 6, strip.height + 2, 3, color);
    }
}

}  // namespace chain_iot::view
