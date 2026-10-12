/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "horizontal_level_view.h"

#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>

using namespace uitk;

namespace {

constexpr int ScreenSize               = view::LevelScreenSize;
constexpr std::uint32_t RenderInterval = 16;

constexpr int BubbleDefaultSize    = 68;
constexpr int BubbleMaximumSize    = 102;
constexpr int FixedCrossLength     = 182;
constexpr int BubbleCrossLength    = 24;
constexpr int CrossThickness       = 2;
constexpr float GyroMaximumDps     = 180.0F;
constexpr float PositionDuration   = 0.1F;
constexpr float BubbleSizeDuration = 0.3F;
constexpr float PaletteDuration    = 0.35F;
constexpr float EntryFadeDuration  = 0.28F;

constexpr float StablePlanarEnterThreshold = 0.04F;
constexpr float StablePlanarExitThreshold  = 0.08F;
constexpr float StableAccelZEnterThreshold = 0.12F;
constexpr float StableAccelZExitThreshold  = 0.18F;
constexpr float StableGyroEnterDps         = 2.0F;
constexpr float StableGyroExitDps          = 5.0F;
constexpr std::uint32_t StableHoldDuration = 350;

constexpr int AccelXLabelX                    = 128;
constexpr int AccelXLabelY                    = 33;
constexpr int AccelYLabelX                    = 33;
constexpr int AccelYLabelY                    = 128;
constexpr int AccelZLabelX                    = 128;
constexpr int AccelZLabelY                    = 191;
constexpr std::uint32_t BackgroundColor       = 0xFFA879;
constexpr std::uint32_t BubbleColor           = 0xFDFCE7;
constexpr std::uint32_t CrossColor            = 0xDE8250;
constexpr std::uint32_t LabelColor            = 0x994D24;
constexpr std::uint32_t StableBackgroundColor = 0xB3FFA4;
constexpr std::uint32_t StableBubbleColor     = 0x65C865;
constexpr std::uint32_t StableFixedCrossColor = 0x3EA13E;
constexpr std::uint32_t StableLabelColor      = 0x1B681B;

const lgfx::LVGLFont DataFont{&font_noto_sans_semibold_14, lgfx::lvgl_font_blend_t::destination};

float clampNormalized(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

float removeNegativeZero(float value)
{
    return std::abs(value) < 0.05F ? 0.0F : value;
}

void prepareAnimation(AnimateValue& animation, float duration)
{
    auto& options          = animation.easingOptions();
    options.duration       = duration;
    options.easingFunction = ease::linear;
    animation.teleport(0.0F);
}

void prepareModeTransitionAnimation(AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = view::ModeTransitionVisualDuration;
    options.bounce         = view::ModeTransitionBounce;
    animation.teleport(0.0F);
}

void prepareEntryFadeAnimation(AnimateValue& animation)
{
    auto& options          = animation.easingOptions();
    options.duration       = EntryFadeDuration;
    options.easingFunction = ease::ease_out_quad;
    animation.delay        = 0.0F;
    animation.teleport(0.0F);
}

void prepareColorAnimation(color::AnimateRgb_t& animation, std::uint32_t initialColor)
{
    animation.duration       = PaletteDuration;
    animation.easingFunction = ease::ease_out_quad;
    animation.begin();
    animation.teleport(initialColor);
    animation.update();
}

}  // namespace

namespace view {

HorizontalLevelView::~HorizontalLevelView()
{
    destroy();
}

void HorizontalLevelView::init()
{
    destroy();
    prepareAnimation(_bubble_x, PositionDuration);
    prepareAnimation(_bubble_y, PositionDuration);
    prepareAnimation(_bubble_size, BubbleSizeDuration);
    prepareEntryFadeAnimation(_entry_mask_opacity);
    prepareModeTransitionAnimation(_transition_offset);
    prepareColorAnimation(_background_color, BackgroundColor);
    prepareColorAnimation(_bubble_color, BubbleColor);
    prepareColorAnimation(_fixed_cross_color, CrossColor);
    prepareColorAnimation(_label_color, LabelColor);
    _show_data_labels           = true;
    _has_imu_data               = false;
    _bubble_animating           = false;
    _is_stable                  = false;
    _stability_pending          = false;
    _palette_animating          = false;
    _entry_fade_animating       = false;
    _transition_visible         = false;
    _transition_animating       = false;
    _render_requested           = true;
    _stable_since               = 0;
    _last_bubble_render         = 0;
    _last_palette_render        = 0;
    _last_entry_fade_render     = 0;
    _last_transition_render     = 0;
    _rendered_bubble_size       = BubbleDefaultSize;
    _rendered_bubble_offset_x   = 0;
    _rendered_bubble_offset_y   = 0;
    _rendered_transition_offset = 0.0F;
    _rendered_mask_opacity      = 0;
    _surface_transition         = {};

    setAccelLabelValues(0.0F, 0.0F, 1.0F);
    const std::uint32_t now = GetHAL().millis();
    applyBubbleState(now, true);
    applyPalette(now, true);
    _bubble_animating  = false;
    _palette_animating = false;
}

void HorizontalLevelView::update(std::uint32_t currentTime)
{
    if (_bubble_animating) {
        applyBubbleState(currentTime);
    }
    if (_palette_animating) {
        applyPalette(currentTime);
    }
    if (_entry_fade_animating) {
        applyEntryFade(currentTime);
    }
    if (_transition_animating) {
        applyModeTransition(currentTime);
    }
}

void HorizontalLevelView::setImuData(const ImuData& data)
{
    const float gyro_motion = std::sqrt(data.gyroX * data.gyroX + data.gyroY * data.gyroY + data.gyroZ * data.gyroZ);
    const float bubble_x    = clampNormalized(data.accelX);
    const float bubble_y    = clampNormalized(data.accelY);
    const float bubble_size = std::clamp(gyro_motion / GyroMaximumDps, 0.0F, 1.0F);

    updateStability(data, gyro_motion);

    if (!_has_imu_data) {
        _bubble_x.teleport(bubble_x);
        _bubble_y.teleport(bubble_y);
        _bubble_size.teleport(bubble_size);
        _has_imu_data = true;
        applyBubbleState(GetHAL().millis(), true);
    } else {
        bool bubble_changed = false;
        if (bubble_x != _bubble_x.end) {
            _bubble_x      = bubble_x;
            bubble_changed = true;
        }
        if (bubble_y != _bubble_y.end) {
            _bubble_y      = bubble_y;
            bubble_changed = true;
        }
        if (bubble_size != _bubble_size.end) {
            _bubble_size   = bubble_size;
            bubble_changed = true;
        }
        _bubble_animating = _bubble_animating || bubble_changed;
    }

    setAccelLabelValues(data.accelX, data.accelY, data.accelZ);
}

void HorizontalLevelView::setDataLabelsVisible(bool visible)
{
    if (_show_data_labels == visible) {
        return;
    }
    _show_data_labels = visible;
    _render_requested = true;
}

void HorizontalLevelView::startEntryFade()
{
    _entry_mask_opacity.teleport(255.0F);
    _rendered_mask_opacity = 255;
    _entry_mask_opacity    = 0.0F;
    _entry_fade_animating  = true;
    applyEntryFade(GetHAL().millis(), true);
}

void HorizontalLevelView::startVerticalExitTransition(const LevelSurfaceTransition& transition)
{
    _surface_transition = transition;
    _transition_offset.teleport(transition.offset);
    _rendered_transition_offset = transition.offset;
    _transition_offset          = static_cast<float>(ScreenSize);
    _transition_visible         = true;
    _transition_animating       = true;
    applyModeTransition(GetHAL().millis(), true);
}

void HorizontalLevelView::destroy()
{
    _has_imu_data               = false;
    _bubble_animating           = false;
    _is_stable                  = false;
    _stability_pending          = false;
    _palette_animating          = false;
    _entry_fade_animating       = false;
    _transition_visible         = false;
    _transition_animating       = false;
    _render_requested           = true;
    _stable_since               = 0;
    _last_bubble_render         = 0;
    _last_palette_render        = 0;
    _last_entry_fade_render     = 0;
    _last_transition_render     = 0;
    _rendered_bubble_size       = BubbleDefaultSize;
    _rendered_bubble_offset_x   = 0;
    _rendered_bubble_offset_y   = 0;
    _rendered_transition_offset = 0.0F;
    _rendered_mask_opacity      = 0;
    _surface_transition         = {};
    _entry_mask_opacity.pause();
}

void HorizontalLevelView::draw(lgfx::LGFX_Sprite& canvas)
{
    canvas.fillScreen(_background_color.toHex());

    const int bubble_x = (ScreenSize - _rendered_bubble_size) / 2 + _rendered_bubble_offset_x;
    const int bubble_y = (ScreenSize - _rendered_bubble_size) / 2 + _rendered_bubble_offset_y;
    canvas.fillSmoothRoundRect(bubble_x, bubble_y, _rendered_bubble_size, _rendered_bubble_size,
                               _rendered_bubble_size / 2, _bubble_color.toHex());

    const int bubble_center_x       = ScreenSize / 2 + _rendered_bubble_offset_x;
    const int bubble_center_y       = ScreenSize / 2 + _rendered_bubble_offset_y;
    const std::uint32_t cross_color = _fixed_cross_color.toHex();
    canvas.fillSmoothRoundRect(bubble_center_x - BubbleCrossLength / 2, bubble_center_y - CrossThickness / 2,
                               BubbleCrossLength, CrossThickness, CrossThickness / 2, cross_color);
    canvas.fillSmoothRoundRect(bubble_center_x - CrossThickness / 2, bubble_center_y - BubbleCrossLength / 2,
                               CrossThickness, BubbleCrossLength, CrossThickness / 2, cross_color);

    canvas.fillSmoothRoundRect((ScreenSize - FixedCrossLength) / 2, (ScreenSize - CrossThickness) / 2, FixedCrossLength,
                               CrossThickness, CrossThickness / 2, cross_color);
    canvas.fillSmoothRoundRect((ScreenSize - CrossThickness) / 2, (ScreenSize - FixedCrossLength) / 2, CrossThickness,
                               FixedCrossLength, CrossThickness / 2, cross_color);

    if (_show_data_labels) {
        canvas.setFont(&DataFont);
        canvas.setTextColor(_label_color.toHex());
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.drawString(_accel_x_text.data(), AccelXLabelX, AccelXLabelY);
        canvas.drawString(_accel_y_text.data(), AccelYLabelX, AccelYLabelY);
        canvas.drawString(_accel_z_text.data(), AccelZLabelX, AccelZLabelY);
    }

    if (_transition_visible) {
        const LevelTransitionOffset offset =
            levelTransitionOffset(_rendered_transition_offset, _surface_transition.lockedOrientation);
        drawLevelSurface(canvas, _surface_transition.orientation, offset, _surface_transition.color);
    }

    if (_rendered_mask_opacity == 255U) {
        canvas.fillScreen(_background_color.toHex());
    } else if (_rendered_mask_opacity != 0U) {
        canvas.fillRectAlpha(0, 0, ScreenSize, ScreenSize, _rendered_mask_opacity, _background_color.toHex());
    }
}

bool HorizontalLevelView::renderRequested() const
{
    return _render_requested;
}

void HorizontalLevelView::clearRenderRequest()
{
    _render_requested = false;
}

void HorizontalLevelView::applyBubbleState(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_bubble_render < RenderInterval) {
        return;
    }

    const float animated_size = _bubble_size;
    const int bubble_size     = static_cast<int>(
        std::lround(BubbleDefaultSize + static_cast<float>(BubbleMaximumSize - BubbleDefaultSize) * animated_size));
    const int maximum_offset  = (FixedCrossLength - bubble_size) / 2;
    _rendered_bubble_size     = bubble_size;
    _rendered_bubble_offset_x = static_cast<int>(std::lround(maximum_offset * static_cast<float>(_bubble_x)));
    _rendered_bubble_offset_y = static_cast<int>(std::lround(maximum_offset * static_cast<float>(_bubble_y)));
    _last_bubble_render       = currentTime;
    if (!force) {
        _bubble_animating = !_bubble_x.done() || !_bubble_y.done() || !_bubble_size.done();
    }
    _render_requested = true;
}

void HorizontalLevelView::applyPalette(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_palette_render < RenderInterval) {
        return;
    }

    _background_color.update();
    _bubble_color.update();
    _fixed_cross_color.update();
    _label_color.update();
    _last_palette_render = currentTime;
    _render_requested    = true;
    _palette_animating =
        !_background_color.done() || !_bubble_color.done() || !_fixed_cross_color.done() || !_label_color.done();
}

void HorizontalLevelView::applyEntryFade(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_entry_fade_render < RenderInterval) {
        return;
    }

    _rendered_mask_opacity =
        static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<float>(_entry_mask_opacity), 0.0F, 255.0F)));
    _last_entry_fade_render = currentTime;
    if (!force) {
        _entry_fade_animating = !_entry_mask_opacity.done();
    }
    _render_requested = true;
}

void HorizontalLevelView::applyModeTransition(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_transition_render < RenderInterval) {
        return;
    }

    _rendered_transition_offset = _transition_offset;
    _last_transition_render     = currentTime;
    if (!force) {
        _transition_animating = !_transition_offset.done();
        if (!_transition_animating) {
            _transition_visible = false;
        }
    }
    _render_requested = true;
}

void HorizontalLevelView::setAccelLabelValues(float accelX, float accelY, float accelZ)
{
    std::snprintf(_accel_x_text.data(), _accel_x_text.size(), "X: %.1f", removeNegativeZero(accelX));
    std::snprintf(_accel_y_text.data(), _accel_y_text.size(), "Y: %.1f", removeNegativeZero(accelY));
    std::snprintf(_accel_z_text.data(), _accel_z_text.size(), "Z: %.1f", removeNegativeZero(accelZ));
    _render_requested = true;
}

void HorizontalLevelView::updateStability(const ImuData& data, float gyroMotion)
{
    const float planar_accel  = std::hypot(data.accelX, data.accelY);
    const float accel_z_error = std::abs(std::abs(data.accelZ) - 1.0F);

    if (_is_stable) {
        if (planar_accel > StablePlanarExitThreshold || accel_z_error > StableAccelZExitThreshold ||
            gyroMotion > StableGyroExitDps) {
            setStable(false);
        }
        return;
    }

    const bool stable_sample = planar_accel <= StablePlanarEnterThreshold &&
                               accel_z_error <= StableAccelZEnterThreshold && gyroMotion <= StableGyroEnterDps;
    if (!stable_sample) {
        _stability_pending = false;
        return;
    }

    const std::uint32_t now = GetHAL().millis();
    if (!_stability_pending) {
        _stability_pending = true;
        _stable_since      = now;
        return;
    }

    if (now - _stable_since >= StableHoldDuration) {
        setStable(true);
    }
}

void HorizontalLevelView::setStable(bool stable)
{
    if (_is_stable == stable) {
        return;
    }

    _is_stable         = stable;
    _stability_pending = false;
    _palette_animating = true;
    _background_color.move(stable ? StableBackgroundColor : BackgroundColor);
    _bubble_color.move(stable ? StableBubbleColor : BubbleColor);
    _fixed_cross_color.move(stable ? StableFixedCrossColor : CrossColor);
    _label_color.move(stable ? StableLabelColor : LabelColor);
}

}  // namespace view
