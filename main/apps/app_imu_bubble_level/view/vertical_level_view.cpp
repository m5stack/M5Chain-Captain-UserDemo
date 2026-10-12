/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "vertical_level_view.h"

#include <algorithm>
#include <apps/common/gfx/alpha_renderer.h>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>

using namespace uitk;

namespace {

constexpr int ScreenSize   = view::LevelScreenSize;
constexpr int ScreenCenter = ScreenSize / 2;

constexpr int TickWidth  = 30;
constexpr int TickHeight = 2;
constexpr int TickY      = 119;
constexpr int LeftTickX  = 13;
constexpr int RightTickX = 196;

constexpr int AngleSourceWidth  = 180;
constexpr int AngleSourceHeight = 60;
constexpr int AngleSourceTextY  = 6;

constexpr float Pi                              = 3.14159265358979323846F;
constexpr float AngleDuration                   = 0.1F;
constexpr float PaletteDuration                 = 0.35F;
constexpr float ScreenOrientationVisualDuration = 0.35F;
constexpr float ScreenOrientationBounce         = 0.2F;
constexpr std::uint32_t RenderInterval          = 16;
constexpr float ScreenOrientationSwitchDegrees  = 55.0F;

constexpr float StableAngleEnterDegrees    = 2.0F;
constexpr float StableAngleExitDegrees     = 4.0F;
constexpr float StableZRatioEnter          = 0.05F;
constexpr float StableZRatioExit           = 0.1F;
constexpr float StableMagnitudeEnterError  = 0.2F;
constexpr float StableMagnitudeExitError   = 0.3F;
constexpr float StableGyroEnterDps         = 2.0F;
constexpr float StableGyroExitDps          = 5.0F;
constexpr std::uint32_t StableHoldDuration = 600;

constexpr std::uint32_t BackgroundColor       = 0xFFA879;
constexpr std::uint32_t SurfaceColor          = 0xFDFCE7;
constexpr std::uint32_t TickColor             = 0xD37744;
constexpr std::uint32_t LabelColor            = 0x994D24;
constexpr std::uint32_t StableBackgroundColor = 0x65C865;
constexpr std::uint32_t StableSurfaceColor    = 0xB3FFA4;
constexpr std::uint32_t StableLabelColor      = 0x37672D;

void prepareAngleAnimation(AnimateValue& animation)
{
    auto& options          = animation.easingOptions();
    options.duration       = AngleDuration;
    options.easingFunction = ease::linear;
    animation.teleport(0.0F);
}

void prepareScreenOrientationAnimation(AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = ScreenOrientationVisualDuration;
    options.bounce         = ScreenOrientationBounce;
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

void prepareColorAnimation(color::AnimateRgb_t& animation, std::uint32_t initialColor)
{
    animation.duration       = PaletteDuration;
    animation.easingFunction = ease::ease_out_quad;
    animation.begin();
    animation.teleport(initialColor);
    animation.update();
}

float verticalAngleFromBaseline(float rawAngle, float baseline)
{
    // A level measures an undirected line, so the reading repeats every half turn.
    return std::remainder(rawAngle - baseline, 180.0F);
}

float unwrapAngleNear(float angle, float reference)
{
    return reference + std::remainder(angle - reference, 360.0F);
}

float vectorMagnitude(const ImuData& data)
{
    return std::sqrt(data.accelX * data.accelX + data.accelY * data.accelY + data.accelZ * data.accelZ);
}

struct FloatPoint {
    float x = 0.0F;
    float y = 0.0F;
};

FloatPoint rotateAroundScreenCenter(const FloatPoint& point, float angle)
{
    const float radians = angle * Pi / 180.0F;
    const float cosine  = std::cos(radians);
    const float sine    = std::sin(radians);
    const float x       = point.x - static_cast<float>(ScreenCenter);
    const float y       = point.y - static_cast<float>(ScreenCenter);
    return {
        static_cast<float>(ScreenCenter) + cosine * x - sine * y,
        static_cast<float>(ScreenCenter) + sine * x + cosine * y,
    };
}

void drawTick(lgfx::LGFX_Sprite& canvas, const FloatPoint& start, const FloatPoint& end, std::uint32_t color,
              const view::LevelTransitionOffset& offset)
{
    canvas.drawWideLine(static_cast<int>(std::lround(start.x + offset.x)),
                        static_cast<int>(std::lround(start.y + offset.y)),
                        static_cast<int>(std::lround(end.x + offset.x)),
                        static_cast<int>(std::lround(end.y + offset.y)), static_cast<float>(TickHeight) / 2.0F, color);
}

std::uint8_t glyphBitsPerPixel(lv_font_glyph_format_t format)
{
    switch (format) {
        case LV_FONT_GLYPH_FORMAT_A1:
            return 1;
        case LV_FONT_GLYPH_FORMAT_A2:
            return 2;
        case LV_FONT_GLYPH_FORMAT_A3:
            return 3;
        case LV_FONT_GLYPH_FORMAT_A4:
            return 4;
        case LV_FONT_GLYPH_FORMAT_A8:
            return 8;
        default:
            return 0;
    }
}

std::uint8_t glyphAlpha(const std::uint8_t* bitmap, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                        std::uint32_t stride, std::uint8_t bitsPerPixel)
{
    const std::uint32_t bit_offset = stride == 0 ? (y * width + x) * bitsPerPixel : y * stride * 8U + x * bitsPerPixel;
    const std::uint32_t byte_index = bit_offset / 8;
    const std::uint32_t intra_byte = bit_offset % 8;
    if (bitsPerPixel == 8 && intra_byte == 0) {
        return bitmap[byte_index];
    }

    const std::uint16_t packed = static_cast<std::uint16_t>(bitmap[byte_index]) << 8 |
                                 (intra_byte + bitsPerPixel > 8 ? bitmap[byte_index + 1] : 0);
    const std::uint8_t raw =
        static_cast<std::uint8_t>((packed >> (16 - intra_byte - bitsPerPixel)) & ((1U << bitsPerPixel) - 1U));
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(raw) * 255U) / ((1U << bitsPerPixel) - 1U));
}

}  // namespace

namespace view {

VerticalLevelView::~VerticalLevelView()
{
    destroy();
}

void VerticalLevelView::init()
{
    destroy();
    prepareAngleAnimation(_angle);
    prepareAngleAnimation(_orientation);
    prepareScreenOrientationAnimation(_screen_orientation);
    prepareModeTransitionAnimation(_transition_y);
    prepareColorAnimation(_background_color, BackgroundColor);
    prepareColorAnimation(_surface_color, SurfaceColor);
    prepareColorAnimation(_tick_color, TickColor);
    prepareColorAnimation(_label_color, LabelColor);
    _has_imu_data                = false;
    _orientation_animating       = false;
    _is_stable                   = false;
    _stability_pending           = false;
    _palette_animating           = false;
    _baseline_initialized        = false;
    _transition_animating        = false;
    _render_requested            = true;
    _stable_since                = 0;
    _last_orientation_render     = 0;
    _last_palette_render         = 0;
    _reading_baseline            = 0.0F;
    _rendered_orientation        = 0.0F;
    _rendered_screen_orientation = 0.0F;
    _rendered_transition_y       = 0.0F;
    _displayed_angle             = 361;

    _angle_mask.fill(0);
    const std::uint32_t now = GetHAL().millis();
    applyOrientation(now, true);
    applyPalette(now, true);
    _orientation_animating = false;
    _palette_animating     = false;
}

void VerticalLevelView::update(std::uint32_t currentTime)
{
    if (_orientation_animating || _transition_animating) {
        applyOrientation(currentTime);
    }
    if (_palette_animating) {
        applyPalette(currentTime);
    }
}

void VerticalLevelView::setImuData(const ImuData& data)
{
    // Accelerometer support force points opposite the screen edge that is visually down.
    const float raw_angle   = std::atan2(-data.accelX, -data.accelY) * 180.0F / Pi;
    const float gyro_motion = std::sqrt(data.gyroX * data.gyroX + data.gyroY * data.gyroY + data.gyroZ * data.gyroZ);

    updateStability(data, gyro_motion, raw_angle);

    const float orientation         = _has_imu_data ? unwrapAngleNear(raw_angle, _orientation.end) : raw_angle;
    bool screen_orientation_changed = false;
    if (!_baseline_initialized) {
        // The currently locked screen edge is also the zero-degree reading baseline.
        _reading_baseline = std::round(orientation / 90.0F) * 90.0F;
        _screen_orientation.teleport(_reading_baseline);
        _baseline_initialized = true;
    } else {
        const float screen_orientation = std::round(orientation / 90.0F) * 90.0F;
        if (screen_orientation != _screen_orientation.end &&
            std::abs(orientation - _screen_orientation.end) >= ScreenOrientationSwitchDegrees) {
            _reading_baseline          = screen_orientation;
            _screen_orientation        = screen_orientation;
            screen_orientation_changed = true;
        }
    }
    const float angle = verticalAngleFromBaseline(orientation, _reading_baseline);

    if (!_has_imu_data) {
        _angle.teleport(angle);
        _orientation.teleport(orientation);
        _has_imu_data          = true;
        _orientation_animating = false;
        applyOrientation(GetHAL().millis(), true);
        return;
    }

    bool orientation_changed = screen_orientation_changed;
    if (angle != _angle.end) {
        if (std::abs(angle - _angle.end) > 90.0F) {
            // Crossing +90/-90 only changes the normalized label. The physical horizon remains continuous.
            _angle.teleport(angle);
        } else {
            _angle = angle;
        }
        orientation_changed = true;
    }
    if (orientation != _orientation.end) {
        _orientation        = orientation;
        orientation_changed = true;
    }
    if (orientation_changed) {
        _orientation_animating = true;
    }
}

void VerticalLevelView::setDataLabelsVisible(bool visible)
{
    (void)visible;
}

void VerticalLevelView::startEnterTransition()
{
    _transition_y.teleport(static_cast<float>(ScreenSize));
    _rendered_transition_y = static_cast<float>(ScreenSize);
    _transition_y          = 0.0F;
    _transition_animating  = true;
    applyOrientation(GetHAL().millis(), true);
}

LevelSurfaceTransition VerticalLevelView::surfaceTransition() const
{
    return {
        _rendered_orientation,
        _rendered_screen_orientation,
        _rendered_transition_y,
        _surface_color.toHex(),
    };
}

void VerticalLevelView::destroy()
{
    _has_imu_data                = false;
    _orientation_animating       = false;
    _is_stable                   = false;
    _stability_pending           = false;
    _palette_animating           = false;
    _baseline_initialized        = false;
    _transition_animating        = false;
    _render_requested            = true;
    _stable_since                = 0;
    _last_orientation_render     = 0;
    _last_palette_render         = 0;
    _reading_baseline            = 0.0F;
    _rendered_orientation        = 0.0F;
    _rendered_screen_orientation = 0.0F;
    _rendered_transition_y       = 0.0F;
    _displayed_angle             = 361;
    _angle_mask.fill(0);
    _angle_mask_left   = 0;
    _angle_mask_top    = 0;
    _angle_mask_right  = 0;
    _angle_mask_bottom = 0;
}

void VerticalLevelView::draw(lgfx::LGFX_Sprite& canvas)
{
    canvas.fillScreen(_background_color.toHex());

    const LevelTransitionOffset transition_offset =
        levelTransitionOffset(_rendered_transition_y, _rendered_screen_orientation);
    drawLevelSurface(canvas, _rendered_orientation, transition_offset, _surface_color.toHex());

    // Reference marks follow the current discrete screen orientation, while the horizon and label track gravity
    // continuously.
    const float visual_rotation = -_rendered_screen_orientation;
    const float tick_center_y   = static_cast<float>(TickY) + static_cast<float>(TickHeight) / 2.0F;
    drawTick(canvas, rotateAroundScreenCenter({static_cast<float>(LeftTickX), tick_center_y}, visual_rotation),
             rotateAroundScreenCenter({static_cast<float>(LeftTickX + TickWidth), tick_center_y}, visual_rotation),
             _tick_color.toHex(), transition_offset);
    drawTick(canvas, rotateAroundScreenCenter({static_cast<float>(RightTickX), tick_center_y}, visual_rotation),
             rotateAroundScreenCenter({static_cast<float>(RightTickX + TickWidth), tick_center_y}, visual_rotation),
             _tick_color.toHex(), transition_offset);

    gfx::drawRotatedA8Mask(canvas, _angle_mask.data(), AngleSourceWidth, AngleSourceHeight, AngleSourceWidth,
                           {_angle_mask_left, _angle_mask_top, _angle_mask_right, _angle_mask_bottom},
                           static_cast<float>(ScreenCenter) + transition_offset.x,
                           static_cast<float>(ScreenCenter) + transition_offset.y, -_rendered_orientation,
                           _label_color.toHex());
}

bool VerticalLevelView::renderRequested() const
{
    return _render_requested;
}

void VerticalLevelView::clearRenderRequest()
{
    _render_requested = false;
}

void VerticalLevelView::applyOrientation(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_orientation_render < RenderInterval) {
        return;
    }

    const float animated_angle              = _angle;
    const float animated_orientation        = _orientation;
    const float animated_screen_orientation = _screen_orientation;
    const float animated_transition_y       = _transition_y;
    _last_orientation_render                = currentTime;
    _rendered_orientation                   = animated_orientation;
    _rendered_screen_orientation            = animated_screen_orientation;
    _rendered_transition_y                  = animated_transition_y;
    if (!force) {
        _orientation_animating = !_angle.done() || !_orientation.done() || !_screen_orientation.done();
        _transition_animating  = !_transition_y.done();
    }

    _render_requested       = true;
    const int display_angle = static_cast<int>(std::lround(animated_angle));
    if (display_angle != _displayed_angle) {
        updateAngleSource(display_angle);
    }
}

void VerticalLevelView::applyPalette(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_palette_render < RenderInterval) {
        return;
    }

    _background_color.update();
    _surface_color.update();
    _tick_color.update();
    _label_color.update();
    _last_palette_render = currentTime;
    _render_requested    = true;

    _palette_animating =
        !_background_color.done() || !_surface_color.done() || !_tick_color.done() || !_label_color.done();
}

void VerticalLevelView::updateAngleSource(int displayAngle)
{
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%d\xC2\xB0", displayAngle);

    std::array<std::uint32_t, 16> codepoints = {};
    std::size_t codepoint_count              = 0;
    const auto* cursor                       = reinterpret_cast<const std::uint8_t*>(text);
    while (*cursor != 0 && codepoint_count < codepoints.size()) {
        if (cursor[0] == 0xC2 && cursor[1] == 0xB0) {
            codepoints[codepoint_count++] = 0xB0;
            cursor += 2;
        } else {
            codepoints[codepoint_count++] = *cursor++;
        }
    }

    int text_width = 0;
    for (std::size_t index = 0; index < codepoint_count; ++index) {
        lv_font_glyph_dsc_t glyph = {};
        const std::uint32_t next  = index + 1 < codepoint_count ? codepoints[index + 1] : 0;
        if (lv_font_get_glyph_dsc(&font_noto_sans_semibold_64, &glyph, codepoints[index], next)) {
            text_width += glyph.adv_w;
            lv_font_glyph_release_draw_data(&glyph);
        }
    }

    _angle_mask.fill(0);
    int mask_left   = AngleSourceWidth;
    int mask_top    = AngleSourceHeight;
    int mask_right  = 0;
    int mask_bottom = 0;
    int pen_x       = (AngleSourceWidth - text_width) / 2;
    const int baseline =
        AngleSourceTextY + lv_font_get_line_height(&font_noto_sans_semibold_64) - font_noto_sans_semibold_64.base_line;
    for (std::size_t index = 0; index < codepoint_count; ++index) {
        lv_font_glyph_dsc_t glyph = {};
        const std::uint32_t next  = index + 1 < codepoint_count ? codepoints[index + 1] : 0;
        if (!lv_font_get_glyph_dsc(&font_noto_sans_semibold_64, &glyph, codepoints[index], next)) {
            continue;
        }

        const std::uint8_t bits_per_pixel = glyphBitsPerPixel(glyph.format);
        const auto* bitmap                = static_cast<const std::uint8_t*>(lv_font_get_glyph_static_bitmap(&glyph));
        if (bits_per_pixel != 0 && bitmap != nullptr) {
            const int glyph_x = pen_x + glyph.ofs_x;
            const int glyph_y = baseline - glyph.ofs_y - glyph.box_h;
            for (std::uint32_t y = 0; y < glyph.box_h; ++y) {
                const int destination_y = glyph_y + static_cast<int>(y);
                if (destination_y < 0 || destination_y >= AngleSourceHeight) {
                    continue;
                }
                for (std::uint32_t x = 0; x < glyph.box_w; ++x) {
                    const int destination_x = glyph_x + static_cast<int>(x);
                    if (destination_x < 0 || destination_x >= AngleSourceWidth) {
                        continue;
                    }
                    auto& destination        = _angle_mask[static_cast<std::size_t>(destination_y) * AngleSourceWidth +
                                                    static_cast<std::size_t>(destination_x)];
                    const std::uint8_t alpha = glyphAlpha(bitmap, x, y, glyph.box_w, glyph.stride, bits_per_pixel);
                    destination              = std::max(destination, alpha);
                    if (alpha != 0) {
                        mask_left   = std::min(mask_left, destination_x);
                        mask_top    = std::min(mask_top, destination_y);
                        mask_right  = std::max(mask_right, destination_x + 1);
                        mask_bottom = std::max(mask_bottom, destination_y + 1);
                    }
                }
            }
        }
        pen_x += glyph.adv_w;
        lv_font_glyph_release_draw_data(&glyph);
    }

    _displayed_angle   = displayAngle;
    _angle_mask_left   = static_cast<std::int16_t>(mask_left < mask_right ? mask_left : 0);
    _angle_mask_top    = static_cast<std::int16_t>(mask_top < mask_bottom ? mask_top : 0);
    _angle_mask_right  = static_cast<std::int16_t>(mask_left < mask_right ? mask_right : 0);
    _angle_mask_bottom = static_cast<std::int16_t>(mask_top < mask_bottom ? mask_bottom : 0);
    _render_requested  = true;
}

void VerticalLevelView::updateStability(const ImuData& data, float gyroMotion, float angle)
{
    const float magnitude       = vectorMagnitude(data);
    const float magnitude_error = std::abs(magnitude - 1.0F);
    const float z_ratio         = magnitude > 0.001F ? std::abs(data.accelZ) / magnitude : 1.0F;
    const float angle_error     = std::abs(std::remainder(angle, 90.0F));

    if (_is_stable) {
        if (angle_error > StableAngleExitDegrees || z_ratio > StableZRatioExit ||
            magnitude_error > StableMagnitudeExitError || gyroMotion > StableGyroExitDps) {
            setStable(false);
        }
        return;
    }

    const bool stable_sample = angle_error <= StableAngleEnterDegrees && z_ratio <= StableZRatioEnter &&
                               magnitude_error <= StableMagnitudeEnterError && gyroMotion <= StableGyroEnterDps;
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

void VerticalLevelView::setStable(bool stable)
{
    if (_is_stable == stable) {
        return;
    }

    _is_stable         = stable;
    _stability_pending = false;
    _palette_animating = true;
    _background_color.move(stable ? StableBackgroundColor : BackgroundColor);
    _surface_color.move(stable ? StableSurfaceColor : SurfaceColor);
    _tick_color.move(stable ? StableSurfaceColor : TickColor);
    _label_color.move(stable ? StableLabelColor : LabelColor);
}

}  // namespace view
