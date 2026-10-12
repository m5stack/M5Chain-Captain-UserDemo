/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "joystick_node.h"

#include "../node_draw.h"

#include <M5GFX.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <variant>

namespace {

constexpr int DetailCardWidth              = 92;
constexpr int DetailCardHeight             = 92;
constexpr int DetailCardRadius             = 12;
constexpr int StickRadius                  = 36;
constexpr int StickBorder                  = 4;
constexpr int StickTravel                  = 8;
constexpr std::int16_t AxisMaximum         = 127;
constexpr float PositionDuration           = 0.1F;
constexpr std::uint32_t RenderInterval     = 16;
constexpr std::uint32_t PressedBorderColor = 0xDC5A77;
constexpr std::uint32_t IconCapColor       = 0xF66987;

void preparePosition(uitk::AnimateValue& animation)
{
    auto& options          = animation.easingOptions();
    options.duration       = PositionDuration;
    options.easingFunction = uitk::ease::linear;
    animation.teleport(0.0F);
}

int positionOffset(float value)
{
    return static_cast<int>(std::lround(static_cast<float>(StickTravel) * value));
}

void renderJoystickIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothCircle(centerX, centerY, 13, chain_iot::view::draw::AngleBorderColor);
    canvas.fillSmoothCircle(centerX, centerY, 12, chain_iot::view::draw::DeviceColor);
    canvas.fillSmoothCircle(centerX, centerY, 6, IconCapColor);
}

}  // namespace

namespace chain_iot::view {

JoystickNode::JoystickNode(std::uint32_t id) : _id(id)
{
    preparePosition(_position_x);
    preparePosition(_position_y);
}

std::uint32_t JoystickNode::id() const
{
    return _id;
}

NodeKind JoystickNode::kind() const
{
    return NodeKind::Joystick;
}

std::string_view JoystickNode::title() const
{
    return "Joystick";
}

bool JoystickNode::detailAvailable() const
{
    return true;
}

DetailLayout JoystickNode::detailLayout() const
{
    return DetailLayout::InfoPanel;
}

bool JoystickNode::apply(const ChainSnapshot& snapshot)
{
    const auto* device = findChainDevice(snapshot, _id);
    if (device == nullptr || device->kind != ChainDeviceKind::Joystick) return false;
    const auto* data = std::get_if<ChainJoystickData>(&device->data);
    if (data == nullptr) return false;
    const auto x                = std::clamp<std::int16_t>(data->x, -AxisMaximum, AxisMaximum);
    const auto y                = std::clamp<std::int16_t>(data->y, -AxisMaximum, AxisMaximum);
    const bool position_changed = x != _x || y != _y;
    const bool pressed_changed  = data->pressed != _pressed;
    const bool color_changed    = _rgb_control.setColor(data->rgb);
    _x                          = x;
    _y                          = y;
    _pressed                    = data->pressed;

    const float normalized_x = static_cast<float>(_x) / AxisMaximum;
    const float normalized_y = static_cast<float>(_y) / AxisMaximum;
    if (!_has_position) {
        _position_x.teleport(normalized_x);
        _position_y.teleport(normalized_y);
        _rendered_x           = positionOffset(normalized_x);
        _rendered_y           = positionOffset(normalized_y);
        _last_position_render = snapshot.capturedAt;
        _has_position         = true;
    } else if (position_changed) {
        _position_x = normalized_x;
        _position_y = normalized_y;
    }
    return position_changed || pressed_changed || color_changed;
}

void JoystickNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    _icon_cache.prepare(canvas, renderJoystickIcon);
}

void JoystickNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    _icon_cache.draw(canvas, centerX, centerY, renderJoystickIcon);
}

void JoystickNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, title(), detailAvailable());
    char line[40];
    std::snprintf(line, sizeof(line), "X: %d  Y: %d", static_cast<int>(_x), static_cast<int>(_y));
    draw::drawInfoLine(canvas, panelY, 0, line);
    const auto rgb = _rgb_control.color();
    std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
    draw::drawInfoLine(canvas, panelY, 1, line);
}

bool JoystickNode::handleDetailInput(input::KeyEvent event)
{
    const auto before  = _rgb_control.color();
    const bool handled = _rgb_control.handleInput(event);
    const auto after   = _rgb_control.color();
    if (before.red != after.red || before.green != after.green || before.blue != after.blue) {
        GetHAL().setChainRgb(_id, after);
    }
    return handled;
}

bool JoystickNode::updateDetail(std::uint32_t currentTime)
{
    const bool color_changed = _rgb_control.update(currentTime);
    const float time_seconds = static_cast<float>(currentTime) / 1000.0F;
    _position_x.update(time_seconds);
    _position_y.update(time_seconds);
    if (!_has_position || currentTime - _last_position_render < RenderInterval) return color_changed;
    _last_position_render       = currentTime;
    const int x                 = positionOffset(_position_x.directValue());
    const int y                 = positionOffset(_position_y.directValue());
    const bool position_changed = x != _rendered_x || y != _rendered_y;
    _rendered_x                 = x;
    _rendered_y                 = y;
    return color_changed || position_changed;
}

void JoystickNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    const int card_y        = frame.y + 14;
    const int card_x        = frame.x + 15 - static_cast<int>(std::lround(112.0F * _rgb_control.expansion()));
    const int center_x      = card_x + DetailCardWidth / 2 + _rendered_x;
    const int center_y      = card_y + DetailCardHeight / 2 - _rendered_y;
    const auto border_color = _pressed ? PressedBorderColor : draw::AngleBorderColor;
    const auto stick_color  = _pressed ? draw::DevicePressedColor : draw::DeviceColor;

    canvas.setBaseColor(draw::InfoPanelColor);
    canvas.fillSmoothRoundRect(card_x, card_y, DetailCardWidth, DetailCardHeight, DetailCardRadius, draw::TileColor);
    canvas.setBaseColor(draw::TileColor);
    canvas.fillSmoothCircle(center_x, center_y, StickRadius, border_color);
    canvas.setBaseColor(border_color);
    canvas.fillSmoothCircle(center_x, center_y, StickRadius - StickBorder, stick_color);

    _rgb_control.draw(canvas, {frame.x + 133, card_y, DetailCardWidth, DetailCardHeight},
                      {frame.x + 15, card_y, frame.width - 30, DetailCardHeight});
}

}  // namespace chain_iot::view
