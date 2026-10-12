/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "angle_node.h"

#include "../node_draw.h"

#include <M5GFX.h>
#include <cmath>
#include <cstdio>
#include <variant>

namespace {

constexpr int DetailCardWidth               = 92;
constexpr int DetailCardHeight              = 92;
constexpr int DetailCardRadius              = 12;
constexpr float Pi                          = 3.14159265358979323846F;
constexpr float MarkerRadius                = 2.0F;
constexpr float MarkerInnerDistance         = 12.0F + MarkerRadius;
constexpr float MarkerOuterDistance         = 26.0F - MarkerRadius;
constexpr std::uint32_t MarkerColor         = 0xFCDBE2;
constexpr std::uint32_t AngleUpdateInterval = 16;
constexpr float AngleDuration               = 0.1F;

void renderAngleIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothCircle(centerX, centerY, 12, chain_iot::view::draw::AngleBorderColor);
    canvas.fillSmoothCircle(centerX, centerY, 11, chain_iot::view::draw::DeviceColor);
    canvas.fillSmoothRoundRect(centerX - 1, centerY - 8, 2, 5, 1, chain_iot::view::draw::AngleBorderColor);
}

}  // namespace

namespace chain_iot::view {

AngleNode::AngleNode(std::uint32_t id) : _id(id)
{
    auto& options          = _angle_animation.easingOptions();
    options.duration       = AngleDuration;
    options.easingFunction = uitk::ease::linear;
    _angle_animation.teleport(0.0F);
}

std::uint32_t AngleNode::id() const
{
    return _id;
}

NodeKind AngleNode::kind() const
{
    return NodeKind::Angle;
}

std::string_view AngleNode::title() const
{
    return "Angle";
}

bool AngleNode::detailAvailable() const
{
    return true;
}

DetailLayout AngleNode::detailLayout() const
{
    return DetailLayout::InfoPanel;
}

bool AngleNode::apply(const ChainSnapshot& snapshot)
{
    const auto* device = findChainDevice(snapshot, _id);
    if (device == nullptr || device->kind != ChainDeviceKind::Angle) return false;
    const auto* data = std::get_if<ChainAngleData>(&device->data);
    if (data == nullptr) return false;
    const bool color_changed = _rgb_control.setColor(data->rgb);
    const bool angle_changed = data->angleDegrees != _angle;
    _angle                   = data->angleDegrees;
    if (!_angle_initialized) {
        _display_angle = static_cast<float>(_angle);
        _angle_animation.teleport(_display_angle);
        _last_angle_update = snapshot.capturedAt;
        _angle_initialized = true;
    } else if (angle_changed) {
        _angle_animation = static_cast<float>(_angle);
    }
    return color_changed || angle_changed;
}

void AngleNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    _icon_cache.prepare(canvas, renderAngleIcon);
}

void AngleNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    _icon_cache.draw(canvas, centerX, centerY, renderAngleIcon);
}

void AngleNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, title(), detailAvailable());
    char line[40];
    std::snprintf(line, sizeof(line), "Angle: %u", static_cast<unsigned>(_angle));
    draw::drawInfoLine(canvas, panelY, 0, line);
    const auto rgb = _rgb_control.color();
    std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
    draw::drawInfoLine(canvas, panelY, 1, line);
}

bool AngleNode::handleDetailInput(input::KeyEvent event)
{
    const auto before  = _rgb_control.color();
    const bool handled = _rgb_control.handleInput(event);
    const auto after   = _rgb_control.color();
    if (before.red != after.red || before.green != after.green || before.blue != after.blue) {
        GetHAL().setChainRgb(_id, after);
    }
    return handled;
}

bool AngleNode::updateDetail(std::uint32_t currentTime)
{
    const bool color_changed = _rgb_control.update(currentTime);
    if (!_angle_initialized) return color_changed;
    _angle_animation.update(static_cast<float>(currentTime) / 1000.0F);
    if (currentTime - _last_angle_update < AngleUpdateInterval) return color_changed;
    _last_angle_update = currentTime;

    const float previous = _display_angle;
    _display_angle       = _angle_animation.directValue();
    return color_changed || _display_angle != previous;
}

void AngleNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    const int card_y         = frame.y + 14;
    const int angle_x        = frame.x + 15 - static_cast<int>(std::lround(112.0F * _rgb_control.expansion()));
    const int angle_center_x = angle_x + DetailCardWidth / 2;
    const int angle_center_y = card_y + DetailCardHeight / 2;

    canvas.setBaseColor(draw::InfoPanelColor);
    canvas.fillSmoothRoundRect(angle_x, card_y, DetailCardWidth, DetailCardHeight, DetailCardRadius, draw::TileColor);

    canvas.setBaseColor(draw::TileColor);
    canvas.fillSmoothCircle(angle_center_x, angle_center_y, 36, draw::AngleBorderColor);
    canvas.fillSmoothCircle(angle_center_x, angle_center_y, 33, draw::DeviceColor);

    // The physical cap points down at zero and turns clockwise.
    const float radians = _display_angle * Pi / 180.0F;
    const float sine    = std::sin(radians);
    const float cosine  = std::cos(radians);
    const int inner_x   = angle_center_x - static_cast<int>(std::lround(MarkerInnerDistance * sine));
    const int inner_y   = angle_center_y + static_cast<int>(std::lround(MarkerInnerDistance * cosine));
    const int outer_x   = angle_center_x - static_cast<int>(std::lround(MarkerOuterDistance * sine));
    const int outer_y   = angle_center_y + static_cast<int>(std::lround(MarkerOuterDistance * cosine));

    // drawWideLine resets the clip rectangle internally.
    std::int32_t clip_x, clip_y, clip_width, clip_height;
    canvas.getClipRect(&clip_x, &clip_y, &clip_width, &clip_height);
    canvas.drawWideLine(inner_x, inner_y, outer_x, outer_y, MarkerRadius, MarkerColor);
    canvas.setClipRect(clip_x, clip_y, clip_width, clip_height);

    _rgb_control.draw(canvas, {frame.x + 133, card_y, DetailCardWidth, DetailCardHeight},
                      {frame.x + 15, card_y, frame.width - 30, DetailCardHeight});
}

}  // namespace chain_iot::view
