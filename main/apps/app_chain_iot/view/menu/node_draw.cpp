/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_draw.h"

#include <cstdio>
#include <M5GFX.h>
#include <assets/assets.h>

namespace {

constexpr std::uint32_t TextColor          = 0x6F6B7C;
constexpr std::uint32_t DividerAccentColor = 0xDF6A6A;
constexpr std::uint32_t DividerColor       = 0x5B5768;
constexpr std::uint32_t ActionBackground   = 0xCFD9DE;
constexpr std::uint32_t ActionForeground   = 0x819198;

const lgfx::LVGLFont TitleFont{&font_noto_sans_semibold_24};
const lgfx::LVGLFont InfoFont{&font_noto_sans_semibold_14};

void drawActionHint(lgfx::LGFX_Sprite& canvas, int panelY)
{
    constexpr int X      = 188;
    constexpr int Width  = 43;
    constexpr int Height = 18;
    const int y          = panelY + 171;
    canvas.fillSmoothRoundRect(X, y, Width, Height, 4, ActionBackground);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setTextColor(ActionForeground);
    canvas.setBaseColor(ActionBackground);
    canvas.setFont(&InfoFont);
    canvas.drawString("OK", X + 4, y + 2);
    canvas.fillSmoothRoundRect(X + 29, y + 4, 2, 6, 1, ActionForeground);
    canvas.fillSmoothRoundRect(X + 36, y + 8, 2, 6, 1, ActionForeground);
    canvas.fillSmoothRoundRect(X + 29, y + 4, 6, 2, 1, ActionForeground);
    canvas.fillSmoothRoundRect(X + 32, y + 12, 6, 2, 1, ActionForeground);
}

}  // namespace

namespace chain_iot::view::draw {

void drawInfoChrome(lgfx::LGFX_Sprite& canvas, int panelY, std::string_view title, bool showActionHint)
{
    (void)showActionHint;
    canvas.setTextSize(1);
    canvas.setTextColor(TextColor);
    canvas.setBaseColor(InfoPanelColor);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setFont(&TitleFont);
    canvas.drawString(title.data(), 26, panelY + 59);
    canvas.fillRect(12, panelY + 78, 6, 16, DividerAccentColor);
    canvas.fillRect(18, panelY + 93, 207, 1, DividerColor);
    // The detail action hint is intentionally hidden while the Chain IoT
    // information bar is used as the persistent background layer.
    // if (showActionHint) drawActionHint(canvas, panelY);
}

void drawInfoLine(lgfx::LGFX_Sprite& canvas, int panelY, int line, const char* text)
{
    const int marker_y = panelY + 116 + line * 24;
    canvas.fillRect(19, marker_y, 4, 4, TextColor);
    canvas.setTextSize(1);
    canvas.setTextColor(TextColor);
    canvas.setBaseColor(InfoPanelColor);
    canvas.setTextDatum(lgfx::textdatum_t::top_left);
    canvas.setFont(&InfoFont);
    canvas.drawString(text, 28, marker_y - 6);
}

}  // namespace chain_iot::view::draw
