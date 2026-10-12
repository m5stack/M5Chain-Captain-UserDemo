/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "alpha_renderer.h"

#include <M5GFX.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tools/fpm/fixed.hpp>

namespace {

constexpr float Pi = 3.14159265358979323846F;

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
    const std::uint32_t byte_index = bit_offset / 8U;
    const std::uint32_t intra_byte = bit_offset % 8U;
    if (bitsPerPixel == 8U && intra_byte == 0U) {
        return bitmap[byte_index];
    }

    const std::uint16_t packed = static_cast<std::uint16_t>(bitmap[byte_index]) << 8U |
                                 (intra_byte + bitsPerPixel > 8U ? bitmap[byte_index + 1U] : 0U);
    const std::uint8_t raw =
        static_cast<std::uint8_t>((packed >> (16U - intra_byte - bitsPerPixel)) & ((1U << bitsPerPixel) - 1U));
    return static_cast<std::uint8_t>((static_cast<std::uint32_t>(raw) * 255U) / ((1U << bitsPerPixel) - 1U));
}

std::uint32_t nextCodepoint(const char*& cursor)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(cursor);
    if (bytes[0] == 0U) {
        return 0U;
    }
    if ((bytes[0] & 0x80U) == 0U) {
        ++cursor;
        return bytes[0];
    }
    if ((bytes[0] & 0xE0U) == 0xC0U && bytes[1] != 0U) {
        cursor += 2;
        return (static_cast<std::uint32_t>(bytes[0] & 0x1FU) << 6U) | (bytes[1] & 0x3FU);
    }
    if ((bytes[0] & 0xF0U) == 0xE0U && bytes[1] != 0U && bytes[2] != 0U) {
        cursor += 3;
        return (static_cast<std::uint32_t>(bytes[0] & 0x0FU) << 12U) |
               (static_cast<std::uint32_t>(bytes[1] & 0x3FU) << 6U) | (bytes[2] & 0x3FU);
    }
    if ((bytes[0] & 0xF8U) == 0xF0U && bytes[1] != 0U && bytes[2] != 0U && bytes[3] != 0U) {
        cursor += 4;
        return (static_cast<std::uint32_t>(bytes[0] & 0x07U) << 18U) |
               (static_cast<std::uint32_t>(bytes[1] & 0x3FU) << 12U) |
               (static_cast<std::uint32_t>(bytes[2] & 0x3FU) << 6U) | (bytes[3] & 0x3FU);
    }

    ++cursor;
    return 0xFFFDU;
}

std::uint32_t peekCodepoint(const char* cursor)
{
    return nextCodepoint(cursor);
}

bool intersectSourceRange(float coefficient, float offset, float sourceMinimum, float sourceMaximum,
                          float& destinationMinimum, float& destinationMaximum)
{
    constexpr float MinimumCoefficient = 0.000001F;
    if (std::abs(coefficient) < MinimumCoefficient) {
        return offset >= sourceMinimum && offset <= sourceMaximum;
    }

    float first  = (sourceMinimum - offset) / coefficient;
    float second = (sourceMaximum - offset) / coefficient;
    if (first > second) {
        std::swap(first, second);
    }
    destinationMinimum = std::max(destinationMinimum, first);
    destinationMaximum = std::min(destinationMaximum, second);
    return destinationMinimum <= destinationMaximum;
}

void rotateQuarterTurn(int x, int y, int quarterTurns, int& rotatedX, int& rotatedY)
{
    switch ((quarterTurns % 4 + 4) % 4) {
        case 1:
            rotatedX = -y;
            rotatedY = x;
            break;
        case 2:
            rotatedX = -x;
            rotatedY = -y;
            break;
        case 3:
            rotatedX = y;
            rotatedY = -x;
            break;
        default:
            rotatedX = x;
            rotatedY = y;
            break;
    }
}

}  // namespace

namespace gfx {

void drawRotatedA8Mask(lgfx::LGFX_Sprite& canvas, const std::uint8_t* mask, int width, int height, int stride,
                       const AlphaMaskBounds& bounds, float centerX, float centerY, float angle, std::uint32_t color,
                       std::uint8_t opacity)
{
    if (mask == nullptr || width <= 1 || height <= 1 || stride < width || opacity == 0U ||
        bounds.left >= bounds.right || bounds.top >= bounds.bottom) {
        return;
    }

    const float radians          = angle * Pi / 180.0F;
    const float cosine           = std::cos(radians);
    const float sine             = std::sin(radians);
    const float half_width       = static_cast<float>(width) / 2.0F;
    const float half_height      = static_cast<float>(height) / 2.0F;
    const float source_minimum_x = static_cast<float>(std::max(0, bounds.left - 1));
    const float source_maximum_x = static_cast<float>(std::min(width - 1, bounds.right));
    const float source_minimum_y = static_cast<float>(std::max(0, bounds.top - 1));
    const float source_maximum_y = static_cast<float>(std::min(height - 1, bounds.bottom));
    const fpm::fixed_16_16 source_step_x(cosine);
    const fpm::fixed_16_16 source_step_y(-sine);
    const int canvas_width  = canvas.width();
    const int canvas_height = canvas.height();

    for (int y = 0; y < canvas_height; ++y) {
        const float destination_y = static_cast<float>(y) + 0.5F - centerY;
        float destination_minimum = -std::numeric_limits<float>::infinity();
        float destination_maximum = std::numeric_limits<float>::infinity();
        if (!intersectSourceRange(cosine, sine * destination_y + half_width - 0.5F, source_minimum_x, source_maximum_x,
                                  destination_minimum, destination_maximum) ||
            !intersectSourceRange(-sine, cosine * destination_y + half_height - 0.5F, source_minimum_y,
                                  source_maximum_y, destination_minimum, destination_maximum)) {
            continue;
        }

        const int start_x = std::max(0, static_cast<int>(std::ceil(centerX + destination_minimum - 0.5F)));
        const int end_x =
            std::min(canvas_width, static_cast<int>(std::floor(centerX + destination_maximum - 0.5F)) + 1);
        if (start_x >= end_x) {
            continue;
        }

        const float destination_x = static_cast<float>(start_x) + 0.5F - centerX;
        fpm::fixed_16_16 source_x(cosine * destination_x + sine * destination_y + half_width - 0.5F);
        fpm::fixed_16_16 source_y(-sine * destination_x + cosine * destination_y + half_height - 0.5F);
        for (int x = start_x; x < end_x; ++x) {
            const std::int32_t source_x_raw = source_x.raw_value();
            const std::int32_t source_y_raw = source_y.raw_value();
            const int source_x0             = source_x_raw >> 16;
            const int source_y0             = source_y_raw >> 16;
            const std::uint32_t fraction_x  = static_cast<std::uint32_t>(source_x_raw) >> 8U & 0xFFU;
            const std::uint32_t fraction_y  = static_cast<std::uint32_t>(source_y_raw) >> 8U & 0xFFU;
            std::uint8_t alpha              = 0U;
            if (static_cast<unsigned int>(source_x0) < static_cast<unsigned int>(width - 1) &&
                static_cast<unsigned int>(source_y0) < static_cast<unsigned int>(height - 1)) {
                const auto* source_row = mask + static_cast<std::size_t>(source_y0) * static_cast<std::size_t>(stride) +
                                         static_cast<std::size_t>(source_x0);
                const std::uint32_t top = static_cast<std::uint32_t>(source_row[0]) * (256U - fraction_x) +
                                          static_cast<std::uint32_t>(source_row[1]) * fraction_x;
                const std::uint32_t bottom = static_cast<std::uint32_t>(source_row[stride]) * (256U - fraction_x) +
                                             static_cast<std::uint32_t>(source_row[stride + 1]) * fraction_x;
                alpha = static_cast<std::uint8_t>((top * (256U - fraction_y) + bottom * fraction_y + 32768U) >> 16U);
            }
            alpha = static_cast<std::uint8_t>((static_cast<std::uint32_t>(alpha) * opacity + 127U) / 255U);
            if (alpha != 0U) {
                canvas.fillRectAlpha(x, y, 1, 1, alpha, color);
            }
            source_x += source_step_x;
            source_y += source_step_y;
        }
    }
}

void drawCenteredLvglText(lgfx::LGFX_Sprite& canvas, const char* text, const lv_font_t* font, float baselineCenterX,
                          float baselineCenterY, int quarterTurns, std::uint32_t color, std::uint8_t opacity)
{
    if (text == nullptr || text[0] == '\0' || font == nullptr || opacity == 0U) {
        return;
    }

    float text_width   = 0.0F;
    const char* cursor = text;
    while (*cursor != '\0') {
        const std::uint32_t current = nextCodepoint(cursor);
        const std::uint32_t next    = peekCodepoint(cursor);
        text_width += static_cast<float>(lv_font_get_glyph_width(font, current, next));
    }

    float advance = -text_width * 0.5F;
    cursor        = text;
    while (*cursor != '\0') {
        const std::uint32_t current = nextCodepoint(cursor);
        const std::uint32_t next    = peekCodepoint(cursor);
        const float glyph_advance   = static_cast<float>(lv_font_get_glyph_width(font, current, next));
        const float glyph_width     = static_cast<float>(lv_font_get_glyph_width(font, current, 0U));
        const float glyph_center    = advance + glyph_width * 0.5F;

        lv_font_glyph_dsc_t glyph = {};
        if (lv_font_get_glyph_dsc(font, &glyph, current, 0U)) {
            const std::uint8_t bits_per_pixel = glyphBitsPerPixel(glyph.format);
            const auto* bitmap = static_cast<const std::uint8_t*>(lv_font_get_glyph_static_bitmap(&glyph));
            if (bits_per_pixel != 0U && bitmap != nullptr) {
                const int glyph_origin_x =
                    static_cast<int>(std::lround(glyph_center - glyph_width * 0.5F)) + glyph.ofs_x;
                const int glyph_origin_y = -static_cast<int>(glyph.box_h) - glyph.ofs_y;
                for (std::uint32_t y = 0; y < glyph.box_h; ++y) {
                    for (std::uint32_t x = 0; x < glyph.box_w; ++x) {
                        std::uint8_t alpha = glyphAlpha(bitmap, x, y, glyph.box_w, glyph.stride, bits_per_pixel);
                        alpha = static_cast<std::uint8_t>((static_cast<std::uint32_t>(alpha) * opacity + 127U) / 255U);
                        if (alpha == 0U) {
                            continue;
                        }

                        int rotated_x = 0;
                        int rotated_y = 0;
                        rotateQuarterTurn(glyph_origin_x + static_cast<int>(x), glyph_origin_y + static_cast<int>(y),
                                          quarterTurns, rotated_x, rotated_y);
                        const int destination_x = static_cast<int>(std::lround(baselineCenterX)) + rotated_x;
                        const int destination_y = static_cast<int>(std::lround(baselineCenterY)) + rotated_y;
                        if (destination_x >= 0 && destination_x < canvas.width() && destination_y >= 0 &&
                            destination_y < canvas.height()) {
                            canvas.fillRectAlpha(destination_x, destination_y, 1, 1, alpha, color);
                        }
                    }
                }
            }
            lv_font_glyph_release_draw_data(&glyph);
        }
        advance += glyph_advance;
    }
}

}  // namespace gfx
