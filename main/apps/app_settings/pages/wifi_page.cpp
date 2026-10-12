/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "wifi_page.h"

#include "../view/menu/menu_item.h"

#include <cstdio>
#include <M5GFX.h>
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace {

constexpr char Tag[] = "Settings-WiFi";

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;
constexpr int LineSpacing  = 28;

constexpr std::uint32_t BackgroundColor        = settings::view::DefaultTheme.background;
constexpr std::uint32_t HighlightColor         = settings::view::DefaultTheme.surface;
constexpr std::uint32_t TextColor              = settings::view::DefaultTheme.text;
constexpr std::uint32_t SuccessBackgroundColor = 0xD3FFCB;
constexpr std::uint32_t SuccessTextColor       = 0x37672D;

constexpr int HighlightPanelX      = 11;
constexpr int HighlightPanelWidth  = 218;
constexpr int HighlightPanelHeight = 30;
constexpr int HighlightPanelRadius = 10;

const lgfx::LVGLFont MessageFont{&font_noto_sans_semibold_20};

void prepareText(lgfx::LGFX_Sprite& canvas, std::uint32_t textColor, std::uint32_t baseColor)
{
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setTextColor(textColor);
    canvas.setBaseColor(baseColor);
    canvas.setFont(&MessageFont);
}

void drawHighlightedText(lgfx::LGFX_Sprite& canvas, const char* text, int centerY)
{
    canvas.fillSmoothRoundRect(HighlightPanelX, centerY - HighlightPanelHeight / 2, HighlightPanelWidth,
                               HighlightPanelHeight, HighlightPanelRadius, HighlightColor);
    canvas.setBaseColor(HighlightColor);
    canvas.drawString(text, ScreenWidth / 2, centerY);
    canvas.setBaseColor(BackgroundColor);
}

}  // namespace

namespace settings {

WifiPage::~WifiPage()
{
    if (_ap_started && !GetHAL().stopWifiConfigAp()) {
        mclog::tagWarn(Tag, "failed to stop Wi-Fi configuration AP");
    }
}

void WifiPage::init()
{
    _result                 = SettingsPageResult::Active;
    _configuration_complete = false;
    _ap_started             = GetHAL().startWifiConfigAp();

    if (!_ap_started) mclog::tagError(Tag, "failed to start Wi-Fi configuration AP");
    if (!_embedded) {
        draw(GetHAL().gfxCanvas());
        GetHAL().presentGfx();
    }
}

void WifiPage::handleInput(input::KeyEvent event)
{
    if (event == input::KeyEvent::Home) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        _result = _configuration_complete ? SettingsPageResult::Applied : SettingsPageResult::Cancelled;
    }
}

void WifiPage::update(std::uint32_t currentTime)
{
    (void)currentTime;

    if (!_ap_started || _configuration_complete || GetHAL().getWifiState() != WifiState::Off) {
        return;
    }

    // The config portal closes its AP after accepting and saving the credentials.
    _configuration_complete = true;
    if (!_embedded) {
        draw(GetHAL().gfxCanvas());
        GetHAL().presentGfx();
    }
}

SettingsPageResult WifiPage::result() const
{
    return _result;
}

std::uint32_t WifiPage::transitionBackgroundColor() const
{
    return _configuration_complete ? SuccessBackgroundColor : BackgroundColor;
}

void WifiPage::draw(lgfx::LGFX_Sprite& canvas) const
{
    if (!_ap_started)
        drawStartError(canvas);
    else if (_configuration_complete)
        drawSuccessMessage(canvas);
    else
        drawSetupMessage(canvas);
}

void WifiPage::drawSetupMessage(lgfx::LGFX_Sprite& canvas) const
{
    const WifiStatus status = GetHAL().getWifiStatus();
    canvas.clear(BackgroundColor);
    prepareText(canvas, TextColor, BackgroundColor);

    constexpr int FirstLineY = 73;
    canvas.drawString("Connect to Wi-Fi", ScreenWidth / 2, FirstLineY);
    drawHighlightedText(canvas, status.apSsid.c_str(), FirstLineY + LineSpacing);
    canvas.drawString("then open", ScreenWidth / 2, FirstLineY + LineSpacing * 2);
    drawHighlightedText(canvas, status.apWebUrl.c_str(), FirstLineY + LineSpacing * 3);
}

void WifiPage::drawSuccessMessage(lgfx::LGFX_Sprite& canvas) const
{
    canvas.clear(SuccessBackgroundColor);
    prepareText(canvas, SuccessTextColor, SuccessBackgroundColor);
    canvas.drawString("Wi-Fi configured!", ScreenWidth / 2, ScreenHeight / 2);
}

void WifiPage::drawStartError(lgfx::LGFX_Sprite& canvas) const
{
    canvas.clear(BackgroundColor);
    prepareText(canvas, TextColor, BackgroundColor);

    constexpr int FirstLineY = 106;
    canvas.drawString("Wi-Fi setup unavailable", ScreenWidth / 2, FirstLineY);
    canvas.drawString("Press Home to go back", ScreenWidth / 2, FirstLineY + LineSpacing);
}

}  // namespace settings
