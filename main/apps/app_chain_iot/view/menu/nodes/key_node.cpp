/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_node.h"

#include "../node_draw.h"

#include <M5GFX.h>
#include <cmath>
#include <cstdio>
#include <variant>

namespace {

constexpr int DetailCardWidth                    = 92;
constexpr int DetailCardHeight                   = 92;
constexpr int DetailCardRadius                   = 12;
constexpr int ButtonSize                         = 72;
constexpr int ButtonRadius                       = 9;
constexpr int ButtonBorder                       = 4;
constexpr std::uint32_t ButtonPressedBorderColor = 0xCC919F;

void renderKeyIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    const int x = centerX - 15;
    const int y = centerY - 15;
    canvas.fillSmoothRoundRect(x, y, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothRoundRect(x + 3, y + 3, 24, 24, 4, chain_iot::view::draw::DeviceBorderColor);
    canvas.fillSmoothRoundRect(x + 5, y + 5, 20, 20, 2, chain_iot::view::draw::DeviceColor);
}

}  // namespace

namespace chain_iot::view {

KeyNode::KeyNode(std::uint32_t id) : _id(id)
{
}

std::uint32_t KeyNode::id() const
{
    return _id;
}

NodeKind KeyNode::kind() const
{
    return NodeKind::Key;
}

std::string_view KeyNode::title() const
{
    return "Key";
}

bool KeyNode::detailAvailable() const
{
    return true;
}

DetailLayout KeyNode::detailLayout() const
{
    return DetailLayout::InfoPanel;
}

bool KeyNode::apply(const ChainSnapshot& snapshot)
{
    const auto* device = findChainDevice(snapshot, _id);
    if (device == nullptr || device->kind != ChainDeviceKind::Key) return false;
    const auto* data = std::get_if<ChainKeyData>(&device->data);
    if (data == nullptr) return false;
    const bool color_changed = _rgb_control.setColor(data->rgb);
    const bool state_changed = data->pressed != _pressed;
    _pressed                 = data->pressed;
    return color_changed || state_changed;
}

void KeyNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    _icon_cache.prepare(canvas, renderKeyIcon);
}

void KeyNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    _icon_cache.draw(canvas, centerX, centerY, renderKeyIcon);
}

void KeyNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, title(), detailAvailable());
    draw::drawInfoLine(canvas, panelY, 0, _pressed ? "State: Pressed" : "State: Released");
    char line[40];
    const auto rgb = _rgb_control.color();
    std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
    draw::drawInfoLine(canvas, panelY, 1, line);
}

bool KeyNode::handleDetailInput(input::KeyEvent event)
{
    const auto before  = _rgb_control.color();
    const bool handled = _rgb_control.handleInput(event);
    const auto after   = _rgb_control.color();
    if (before.red != after.red || before.green != after.green || before.blue != after.blue) {
        GetHAL().setChainRgb(_id, after);
    }
    return handled;
}

bool KeyNode::updateDetail(std::uint32_t currentTime)
{
    return _rgb_control.update(currentTime);
}

void KeyNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    const int card_y        = frame.y + 14;
    const int card_x        = frame.x + 15 - static_cast<int>(std::lround(112.0F * _rgb_control.expansion()));
    const int button_x      = card_x + (DetailCardWidth - ButtonSize) / 2;
    const int button_y      = card_y + (DetailCardHeight - ButtonSize) / 2;
    const auto border_color = _pressed ? ButtonPressedBorderColor : draw::DeviceBorderColor;

    canvas.setBaseColor(draw::InfoPanelColor);
    canvas.fillSmoothRoundRect(card_x, card_y, DetailCardWidth, DetailCardHeight, DetailCardRadius, draw::TileColor);
    canvas.setBaseColor(draw::TileColor);
    canvas.fillSmoothRoundRect(button_x, button_y, ButtonSize, ButtonSize, ButtonRadius, border_color);
    canvas.setBaseColor(border_color);
    canvas.fillSmoothRoundRect(button_x + ButtonBorder, button_y + ButtonBorder, ButtonSize - ButtonBorder * 2,
                               ButtonSize - ButtonBorder * 2, ButtonRadius - ButtonBorder,
                               _pressed ? draw::DevicePressedColor : draw::DeviceColor);

    _rgb_control.draw(canvas, {frame.x + 133, card_y, DetailCardWidth, DetailCardHeight},
                      {frame.x + 15, card_y, frame.width - 30, DetailCardHeight});
}

}  // namespace chain_iot::view
