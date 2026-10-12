/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mode_view.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int SurfaceY      = 80;
constexpr int SurfacePivotX = view::LevelScreenSize / 2;
constexpr int ScreenCenter  = view::LevelScreenSize / 2;
constexpr float Pi          = 3.14159265358979323846F;

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

void fillHorizontalSpan(lgfx::LGFX_Sprite& canvas, int y, int startX, int endX, std::uint32_t color)
{
    const int clipped_start = std::clamp(startX, 0, view::LevelScreenSize);
    const int clipped_end   = std::clamp(endX, 0, view::LevelScreenSize);
    if (clipped_start < clipped_end) {
        canvas.fillRect(clipped_start, y, clipped_end - clipped_start, 1, color);
    }
}

void fillVerticalSpan(lgfx::LGFX_Sprite& canvas, int x, int startY, int endY, std::uint32_t color)
{
    const int clipped_start = std::clamp(startY, 0, view::LevelScreenSize);
    const int clipped_end   = std::clamp(endY, 0, view::LevelScreenSize);
    if (clipped_start < clipped_end) {
        canvas.fillRect(x, clipped_start, 1, clipped_end - clipped_start, color);
    }
}

void fillBoundaryPixel(lgfx::LGFX_Sprite& canvas, int x, int y, float coverage, std::uint32_t color)
{
    if (x < 0 || x >= view::LevelScreenSize || y < 0 || y >= view::LevelScreenSize || coverage <= 0.0F) {
        return;
    }

    const std::uint8_t alpha = static_cast<std::uint8_t>(std::lround(std::clamp(coverage, 0.0F, 1.0F) * 255.0F));
    if (alpha == 255) {
        canvas.fillRect(x, y, 1, 1, color);
    } else if (alpha != 0) {
        canvas.fillRectAlpha(x, y, 1, 1, alpha, color);
    }
}

}  // namespace

namespace view {

LevelTransitionOffset levelTransitionOffset(float distance, float lockedOrientation)
{
    const float visual_rotation = -lockedOrientation * Pi / 180.0F;
    return {
        -std::sin(visual_rotation) * distance,
        std::cos(visual_rotation) * distance,
    };
}

void drawLevelSurface(lgfx::LGFX_Sprite& canvas, float orientation, const LevelTransitionOffset& offset,
                      std::uint32_t color)
{
    const float radians  = orientation * Pi / 180.0F;
    const float normal_x = std::sin(radians);
    const float normal_y = std::cos(radians);
    FloatPoint surface_pivot =
        rotateAroundScreenCenter({static_cast<float>(SurfacePivotX), static_cast<float>(SurfaceY)}, -orientation);
    surface_pivot.x += offset.x;
    surface_pivot.y += offset.y;

    // Scan along the dominant normal axis. Each scanline has one partially covered edge pixel, so the boundary stays
    // antialiased without the internal seams produced by a triangle fan.
    if (std::fabs(normal_x) >= std::fabs(normal_y)) {
        for (int y = 0; y < LevelScreenSize; ++y) {
            const float sample_y = static_cast<float>(y) + 0.5F;
            const float boundary = surface_pivot.x - normal_y * (sample_y - surface_pivot.y) / normal_x;
            const int edge       = static_cast<int>(std::floor(boundary));
            if (normal_x > 0.0F) {
                fillBoundaryPixel(canvas, edge, y, static_cast<float>(edge + 1) - boundary, color);
                fillHorizontalSpan(canvas, y, edge + 1, LevelScreenSize, color);
            } else {
                fillHorizontalSpan(canvas, y, 0, edge, color);
                fillBoundaryPixel(canvas, edge, y, boundary - static_cast<float>(edge), color);
            }
        }
    } else {
        for (int x = 0; x < LevelScreenSize; ++x) {
            const float sample_x = static_cast<float>(x) + 0.5F;
            const float boundary = surface_pivot.y - normal_x * (sample_x - surface_pivot.x) / normal_y;
            const int edge       = static_cast<int>(std::floor(boundary));
            if (normal_y > 0.0F) {
                fillBoundaryPixel(canvas, x, edge, static_cast<float>(edge + 1) - boundary, color);
                fillVerticalSpan(canvas, x, edge + 1, LevelScreenSize, color);
            } else {
                fillVerticalSpan(canvas, x, 0, edge, color);
                fillBoundaryPixel(canvas, x, edge, boundary - static_cast<float>(edge), color);
            }
        }
    }
}

}  // namespace view
