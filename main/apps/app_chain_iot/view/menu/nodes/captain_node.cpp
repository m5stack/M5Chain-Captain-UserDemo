/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "captain_node.h"

#include "../node_draw.h"

#include <M5GFX.h>
#include <assets/assets.h>
#include <cstdio>
#include <utility>
#include <apps/app_settings/view/menu/action_item.h>
#include <apps/app_settings/view/menu/switch_item.h>
#include <apps/common/audio/audio.h>

namespace {

const lgfx::LVGLFont TitleFont{&font_noto_sans_semibold_24};
const lgfx::LVGLFont StatusFont{&font_noto_sans_semibold_14};

const char* stateLabel(EzDataState state)
{
    switch (state) {
        case EzDataState::Off:
            return "Off";
        case EzDataState::WaitingForWifi:
            return "Waiting for Wi-Fi";
        case EzDataState::Registering:
            return "Registering";
        case EzDataState::Connecting:
            return "Connecting";
        case EzDataState::Connected:
            return "Connected";
        case EzDataState::Failed:
            return "Unavailable";
    }
    return "Off";
}

void renderCaptainIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    const int x = centerX - 26;
    const int y = centerY - 18;
    canvas.fillSmoothRoundRect(x, y, 52, 36, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothRoundRect(x + 13, y + 5, 26, 26, 1, chain_iot::view::draw::CaptainBodyColor);
    canvas.fillSmoothRoundRect(x + 42, y + 8, 4, 8, 1, chain_iot::view::draw::CaptainPortColor);
    canvas.fillSmoothRoundRect(x + 42, y + 21, 4, 8, 1, chain_iot::view::draw::CaptainPortColor);
    canvas.fillSmoothRoundRect(x + 6, y + 8, 4, 8, 1, chain_iot::view::draw::CaptainPowerColor);
    canvas.fillSmoothRoundRect(x + 6, y + 21, 4, 8, 1, chain_iot::view::draw::CaptainPortColor);
}

}  // namespace

namespace chain_iot::view {

CaptainNode::CaptainNode()
{
    _status_bar.create();
    _status_bar.update();
    _menu.setEmbedded(true);
    std::vector<settings::view::MenuSection> sections;
    auto& section = sections.emplace_back("Captain");
    section.addItem<settings::view::SwitchItem>("EzData upload", GetHAL().deviceSettings().ezDataUploadEnabled,
                                                [this](bool enabled) {
                                                    auto settings                = GetHAL().deviceSettings();
                                                    settings.ezDataUploadEnabled = enabled;
                                                    if (!GetHAL().saveDeviceSettings(settings)) return false;
                                                    if (_configure_item) _configure_item->setEnabled(enabled);
                                                    return true;
                                                });
    _configure_item = &section.addItem<settings::view::ActionItem>(
        "Configure EzData", [this]() { _menu.transitionToPage([this]() { openConfiguration(); }); });
    _configure_item->setEnabled(GetHAL().deviceSettings().ezDataUploadEnabled);
    _menu.init(std::move(sections), false, false, 24);
    _menu.setUnderlayDrawCallback([this](lgfx::LGFX_Sprite& canvas) {
        const auto& theme = settings::view::DefaultTheme;
        _status_bar.draw(canvas, {theme.background, theme.secondaryText, 0xF95757, 0x1AFF66, 0xFF5656});
    });
}

void CaptainNode::openConfiguration()
{
    _detail_dirty = true;
    _page         = DetailPage::WaitingForWifi;
}

void CaptainNode::returnToMenu()
{
    _page = DetailPage::Menu;
    _menu.resume();
    _detail_dirty = true;
}

std::uint32_t CaptainNode::id() const
{
    return 0;
}

NodeKind CaptainNode::kind() const
{
    return NodeKind::Captain;
}

std::string_view CaptainNode::title() const
{
    return "Captain";
}

bool CaptainNode::detailAvailable() const
{
    return true;
}

DetailLayout CaptainNode::detailLayout() const
{
    return DetailLayout::FullScreen;
}

bool CaptainNode::apply(const ChainSnapshot& snapshot)
{
    const std::size_t connected = snapshot.left.size() + snapshot.right.size();
    const auto status           = GetHAL().getChainEzDataStatus();
    const bool changed          = connected != _connected || status.state != _ezdata.state ||
                         status.pairingCode != _ezdata.pairingCode || status.lastError != _ezdata.lastError;
    _connected = connected;
    _ezdata    = status;
    return changed;
}

void CaptainNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    _icon_cache.prepare(canvas, renderCaptainIcon);
}

void CaptainNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    _icon_cache.draw(canvas, centerX, centerY, renderCaptainIcon);
}

void CaptainNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, title(), detailAvailable());
    char line[40];
    std::snprintf(line, sizeof(line), "EzData: %s", stateLabel(_ezdata.state));
    draw::drawInfoLine(canvas, panelY, 0, line);
    std::snprintf(line, sizeof(line), "Connected Chains: %u", static_cast<unsigned>(_connected));
    draw::drawInfoLine(canvas, panelY, 1, line);
}

bool CaptainNode::handleDetailInput(input::KeyEvent event)
{
    if (_page != DetailPage::Menu) {
        if (event == input::KeyEvent::Home) {
            audio::play_ui_sfx(audio::UiSfx::Back);
            returnToMenu();
        }
        return true;
    }
    // The parent owns the Home sound and the return to the Chain menu.
    if (event == input::KeyEvent::Home) return _menu.transitioning();
    return _menu.handleInput(event);
}

bool CaptainNode::updateDetail(std::uint32_t currentTime)
{
    bool changed = false;
    if (_page == DetailPage::Menu) {
        changed = _status_bar.update();
        _menu.update(currentTime);
        changed = _menu.takeRenderRequest() || changed;
    }
    if (_page == DetailPage::WaitingForWifi) {
        switch (GetHAL().getWifiState()) {
            case WifiState::Connected:
                _page         = DetailPage::Guide;
                _detail_dirty = true;
                break;
            case WifiState::Off:
            case WifiState::Failed:
                _page         = DetailPage::WifiRequired;
                _detail_dirty = true;
                break;
            default:
                break;
        }
    }
    return std::exchange(_detail_dirty, false) || changed;
}

void CaptainNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    if (_page == DetailPage::Menu) {
        _menu.draw(canvas);
        return;
    }

    const auto& theme  = settings::view::DefaultTheme;
    const int center_x = frame.x + frame.width / 2;
    const int center_y = frame.y + frame.height / 2;
    if (_page == DetailPage::WifiRequired || _page == DetailPage::WaitingForWifi) {
        canvas.fillRect(frame.x, frame.y, frame.width, frame.height, theme.background);
        canvas.setTextSize(1);
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        canvas.setTextColor(theme.text);
        canvas.setBaseColor(theme.background);
        if (_page == DetailPage::WaitingForWifi) {
            canvas.setFont(&StatusFont);
            canvas.drawString("Connecting Wi-Fi...", center_x, center_y);
            return;
        }
        canvas.setFont(&TitleFont);
        canvas.drawString("Wi-Fi required", center_x, center_y - 28);
        canvas.setFont(&StatusFont);
        canvas.drawString("Open Settings to", center_x, center_y + 10);
        canvas.drawString("configure Wi-Fi", center_x, center_y + 34);
        return;
    }

    const int content_y = frame.y + (frame.height - 184) / 2 - 12;
    canvas.fillRect(frame.x, frame.y, frame.width, frame.height, theme.background);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setTextColor(theme.text);
    canvas.setBaseColor(theme.background);
    canvas.setFont(&TitleFont);
    canvas.drawString("EzData setup", center_x, content_y + 24);
    canvas.setFont(&StatusFont);
    canvas.drawString("1. Open the web client", center_x, content_y + 57);
    canvas.fillSmoothRoundRect(frame.x + 11, content_y + 72, frame.width - 22, 37, 12, theme.surface);
    canvas.setBaseColor(theme.surface);
    canvas.drawString("my.m5stack.com/ezdata2", center_x, content_y + 90);
    canvas.setBaseColor(theme.background);
    canvas.drawString("2. Pair with this code", center_x, content_y + 132);
    canvas.fillSmoothRoundRect(frame.x + 11, content_y + 148, frame.width - 22, 48, 12, theme.surface);
    canvas.setBaseColor(theme.surface);
    const bool has_code = !_ezdata.pairingCode.empty();
    canvas.setFont(has_code ? &TitleFont : &StatusFont);
    const auto* message  = has_code                     ? _ezdata.pairingCode.c_str()
                           : !_ezdata.lastError.empty() ? _ezdata.lastError.c_str()
                                                        : stateLabel(_ezdata.state);
    const int text_width = canvas.textWidth(message);
    if (text_width > frame.width - 40) canvas.setTextSize(static_cast<float>(frame.width - 40) / text_width);
    canvas.drawString(message, center_x, content_y + 172);
    canvas.setTextSize(1);
}

}  // namespace chain_iot::view
