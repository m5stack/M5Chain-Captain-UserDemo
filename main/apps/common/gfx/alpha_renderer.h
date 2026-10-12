/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <lvgl.h>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace gfx {

struct AlphaMaskBounds {
    int left   = 0;
    int top    = 0;
    int right  = 0;
    int bottom = 0;
};

void drawRotatedA8Mask(lgfx::LGFX_Sprite& canvas, const std::uint8_t* mask, int width, int height, int stride,
                       const AlphaMaskBounds& bounds, float centerX, float centerY, float angle, std::uint32_t color,
                       std::uint8_t opacity = 255);

void drawCenteredLvglText(lgfx::LGFX_Sprite& canvas, const char* text, const lv_font_t* font, float baselineCenterX,
                          float baselineCenterY, int quarterTurns, std::uint32_t color, std::uint8_t opacity = 255);

}  // namespace gfx
