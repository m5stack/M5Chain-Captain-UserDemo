/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "slider_item.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {

constexpr int PreferredHeight       = 86;
constexpr int HorizontalInset       = 14;
constexpr int LabelTop              = 12;
constexpr int TrackBottomInset      = 22;
constexpr int RestingTrackHeight    = 10;
constexpr int EditingTrackHeight    = 14;
constexpr int RestingKnobSize       = 18;
constexpr int EditingKnobSize       = 24;
constexpr float KnobEndpointInset   = 0.05F;
constexpr float ProgressDuration    = 0.14F;
constexpr float EditVisualDuration  = 0.38F;
constexpr float EditVisualBounce    = 0.38F;
constexpr float EditVisualMin       = -0.15F;
constexpr float EditVisualMax       = 1.20F;
constexpr float AnimationDifference = 0.0005F;

const lgfx::LVGLFont LabelFont{&font_noto_sans_semibold_20};
const lgfx::LVGLFont ValueFont{&font_noto_sans_semibold_20};

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

SliderItem::SliderItem(std::string_view label, int value, int minimum, int maximum, int step, ChangedCallback onChanged,
                       CommittedCallback onCommitted, CancelledCallback onCancelled)
    : MenuItem(label),
      _minimum(std::min(minimum, maximum)),
      _maximum(std::max(minimum, maximum)),
      _step(std::max(1, step)),
      _on_changed(std::move(onChanged)),
      _on_committed(std::move(onCommitted)),
      _on_cancelled(std::move(onCancelled))
{
    _value          = clampAndQuantize(value);
    _value_snapshot = _value;

    auto& progress_options          = _progress.easingOptions();
    progress_options.duration       = ProgressDuration;
    progress_options.easingFunction = uitk::ease::ease_out_cubic;
    _progress.delay                 = 0.0F;
    _progress.teleport(normalizedValue());

    auto& edit_options          = _edit_mix.springOptions();
    edit_options.duration       = 0.0F;
    edit_options.visualDuration = EditVisualDuration;
    edit_options.bounce         = EditVisualBounce;
    edit_options.restDelta      = 0.01F;
    edit_options.restSpeed      = 0.01F;
    _edit_mix.delay             = 0.0F;
    _edit_mix.teleport(0.0F);

    _last_progress = _progress.directValue();
    _last_edit_mix = _edit_mix.directValue();
}

ItemActivation SliderItem::activate()
{
    return ItemActivation::BeginEdit;
}

int SliderItem::preferredHeight() const
{
    return PreferredHeight;
}

bool SliderItem::adjust(int direction)
{
    if (direction == 0) {
        return false;
    }

    const int old_value = _value;
    setValue(_value + (direction > 0 ? _step : -_step));
    return old_value != _value;
}

void SliderItem::beginEdit()
{
    _value_snapshot = _value;
    _edit_mix       = 1.0F;
}

void SliderItem::commitEdit()
{
    _edit_mix = 0.0F;
    if (_on_committed && !_on_committed(_value)) {
        const int previous_value = _value;
        setValueInternal(_value_snapshot, true, false);
        if (previous_value != _value && _on_cancelled) {
            _on_cancelled(_value);
        }
    }
}

void SliderItem::cancelEdit()
{
    const int previous_value = _value;
    setValueInternal(_value_snapshot, true, false);
    if (previous_value != _value && _on_cancelled) {
        _on_cancelled(_value);
    }
    _edit_mix = 0.0F;
}

bool SliderItem::update(std::uint32_t currentTime)
{
    const float current_time = static_cast<float>(currentTime) / 1000.0F;
    _progress.update(current_time);
    _edit_mix.update(current_time);

    const float progress = _progress.directValue();
    const float edit_mix = _edit_mix.directValue();
    const bool changed   = std::abs(progress - _last_progress) > AnimationDifference ||
                         std::abs(edit_mix - _last_edit_mix) > AnimationDifference;
    _last_progress = progress;
    _last_edit_mix = edit_mix;
    return changed;
}

void SliderItem::draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const
{
    const auto& frame                 = state.frame;
    const float edit_mix              = std::clamp(_last_edit_mix, EditVisualMin, EditVisualMax);
    const std::uint32_t surface_color = blendColor(state.theme.surface, state.theme.surfaceEditing, state.editMix);
    const std::uint32_t text_color    = blendColor(state.theme.text, state.theme.textEditing, state.editMix);
    const std::uint32_t track_color   = blendColor(state.theme.track, state.theme.trackEditing, state.editMix);
    const std::uint32_t accent_color  = blendColor(state.theme.accent, state.theme.accentEditing, state.editMix);
    canvas.fillSmoothRoundRect(frame.x, frame.y, frame.width, frame.height, 16, surface_color);

    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setTextColor(text_color);
    canvas.setBaseColor(surface_color);
    canvas.setFont(&LabelFont);
    canvas.drawString(label().data(), frame.x + HorizontalInset, frame.y + LabelTop);

    char value_text[16] = {};
    std::snprintf(value_text, sizeof(value_text), "%d", _value);
    canvas.setTextDatum(lgfx::textdatum_t::top_right);
    canvas.setFont(&ValueFont);
    canvas.drawString(value_text, frame.x + frame.width - HorizontalInset, frame.y + LabelTop);

    const int track_width     = frame.width - HorizontalInset * 2;
    const int track_height    = roundToInt(static_cast<float>(RestingTrackHeight) +
                                           static_cast<float>(EditingTrackHeight - RestingTrackHeight) * edit_mix);
    const int track_x         = frame.x + HorizontalInset;
    const int track_y         = frame.y + frame.height - TrackBottomInset - track_height / 2;
    const int knob_size       = roundToInt(static_cast<float>(RestingKnobSize) +
                                           static_cast<float>(EditingKnobSize - RestingKnobSize) * edit_mix);
    const float progress      = std::clamp(_last_progress, 0.0F, 1.0F);
    const float knob_progress = KnobEndpointInset + progress * (1.0F - KnobEndpointInset * 2.0F);
    const int knob_center_x   = track_x + roundToInt(static_cast<float>(track_width) * knob_progress);
    const int knob_x          = knob_center_x - knob_size / 2;
    const int knob_y          = track_y + (track_height - knob_size) / 2;
    const int fill_width      = progress <= 0.0F ? 0 : std::max(track_height, knob_center_x - track_x);

    canvas.fillSmoothRoundRect(track_x, track_y, track_width, track_height, track_height / 2, track_color);
    canvas.fillSmoothRoundRect(track_x, track_y, std::min(fill_width, track_width), track_height, track_height / 2,
                               accent_color);
    canvas.fillSmoothRoundRect(knob_x, knob_y, knob_size, knob_size, knob_size / 2, state.theme.controlHighlight);
}

void SliderItem::setValue(int value, bool animated)
{
    setValueInternal(value, animated, true);
}

bool SliderItem::setValueInternal(int value, bool animated, bool notifyChanged)
{
    const int next_value = clampAndQuantize(value);
    if (next_value == _value) {
        return false;
    }

    _value = next_value;
    if (animated) {
        _progress = normalizedValue();
    } else {
        _progress.teleport(normalizedValue());
    }

    if (notifyChanged && _on_changed) {
        _on_changed(_value);
    }
    return true;
}

int SliderItem::clampAndQuantize(int value) const
{
    if (value <= _minimum) {
        return _minimum;
    }
    if (value >= _maximum) {
        return _maximum;
    }

    const int clamped_value   = std::clamp(value, _minimum, _maximum);
    const int offset          = clamped_value - _minimum;
    const int quantized_value = _minimum + ((offset + _step / 2) / _step) * _step;
    return std::clamp(quantized_value, _minimum, _maximum);
}

float SliderItem::normalizedValue() const
{
    if (_maximum == _minimum) {
        return 0.0F;
    }
    return static_cast<float>(_value - _minimum) / static_cast<float>(_maximum - _minimum);
}

}  // namespace settings::view
