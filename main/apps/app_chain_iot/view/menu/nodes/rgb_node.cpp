/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rgb_node.h"

#include "../node_draw.h"

#include <M5GFX.h>
#include <cmath>
#include <cstdio>
#include <variant>

namespace {

using NodeKind = chain_iot::view::NodeKind;

constexpr int DetailCardWidth  = 92;
constexpr int DetailCardHeight = 92;
constexpr int DetailCardRadius = 12;

void renderBuzzerIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothRoundRect(centerX - 11, centerY - 11, 22, 22, 11, 0xA6A6A6U);
    canvas.fillSmoothRoundRect(centerX - 10, centerY - 10, 20, 20, 10, 0xD9D9D9U);
    canvas.fillSmoothRoundRect(centerX - 5, centerY - 6, 10, 2, 1, 0x616161U);
    canvas.fillSmoothRoundRect(centerX - 4, centerY - 2, 8, 2, 1, 0x616161U);
    canvas.fillSmoothRoundRect(centerX - 3, centerY + 3, 6, 3, 1, 0xA5A5A5U);
}

void renderEncoderIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothCircle(centerX, centerY, 15, chain_iot::view::draw::DeviceBorderColor);
    canvas.fillSmoothCircle(centerX, centerY, 13, chain_iot::view::draw::DeviceColor);
    for (const auto x : {centerX - 5, centerX + 5}) {
        for (const auto y : {centerY - 5, centerY + 5}) {
            canvas.fillSmoothCircle(x, y, 3, chain_iot::view::draw::DevicePressedColor);
        }
    }
}

void renderMonoIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    constexpr std::uint32_t colors[] = {0xABC0D3U, 0xFFFFFFU, 0xABC0D3U, 0xFFFFFFU, 0xABC0D3U,
                                        0xFFFFFFU, 0xABC0D3U, 0xFFFFFFU, 0xABC0D3U};
    int index                        = 0;
    for (const auto y : {centerY - 13, centerY - 4, centerY + 5}) {
        for (const auto x : {centerX - 13, centerX - 4, centerX + 5}) {
            canvas.fillSmoothRoundRect(x, y, 8, 8, 1, colors[index++]);
        }
    }
}

void renderRgbIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, 0x3F3F3FU);
    constexpr std::uint32_t colors[] = {0x70ACE5U, 0xA7D66EU, 0xCE96D3U, 0xEC8282U, 0x9E9CE8U,
                                        0xF4B34BU, 0xECDF95U, 0xE0E5E1U, 0x5EDA8DU};
    int index                        = 0;
    for (const auto y : {centerY - 13, centerY - 4, centerY + 5}) {
        for (const auto x : {centerX - 13, centerX - 4, centerX + 5}) {
            canvas.fillSmoothRoundRect(x, y, 8, 8, 1, colors[index++]);
        }
    }
}

void renderPirIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothRoundRect(centerX - 10, centerY - 10, 20, 20, 10, 0xA6A6A6U);
    canvas.fillSmoothRoundRect(centerX - 9, centerY - 9, 18, 18, 9, 0xFFFFFFU);
}

void renderTofIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothRoundRect(centerX - 15, centerY - 15, 30, 30, 4, chain_iot::view::draw::TileColor);
    canvas.fillSmoothRoundRect(centerX - 11, centerY - 9, 22, 18, 5, 0xBCBCBCU);
    canvas.fillSmoothRoundRect(centerX - 5, centerY - 3, 10, 6, 1, 0x3D3D3DU);
}

void drawLargeIcon(lgfx::LGFX_Sprite& canvas, NodeKind kind, int centerX, int centerY, RgbColor color)
{
    constexpr int Size   = 72;
    constexpr int Radius = 10;
    const int left       = centerX - Size / 2;
    const int top        = centerY - Size / 2;

    switch (kind) {
        case NodeKind::Buzzer:
            canvas.fillSmoothRoundRect(left, top, Size, Size, Radius, 0xD9D9D9U);
            canvas.fillSmoothRoundRect(centerX - 26, centerY - 26, 52, 52, 26, 0xA6A6A6U);
            canvas.fillSmoothRoundRect(centerX - 24, centerY - 24, 48, 48, 24, 0xD9D9D9U);
            canvas.fillSmoothRoundRect(centerX - 12, centerY - 14, 24, 5, 2, 0x616161U);
            canvas.fillSmoothRoundRect(centerX - 10, centerY - 4, 19, 5, 2, 0x616161U);
            canvas.fillSmoothRoundRect(centerX - 7, centerY + 7, 14, 7, 3, 0xA5A5A5U);
            break;
        case NodeKind::Encoder:
            // The source icon is a 30 px tile: a D9D9D9 rounded square with a
            // 2 px pink ring inset by 1 px. Scale that geometry by 2.4x.
            canvas.fillSmoothRoundRect(left, top, Size, Size, 10, 0xD9D9D9U);
            canvas.fillSmoothCircle(centerX, centerY, 36, chain_iot::view::draw::DeviceBorderColor);
            canvas.fillSmoothCircle(centerX, centerY, 31, chain_iot::view::draw::DeviceColor);
            for (const auto x : {centerX - 12, centerX + 12}) {
                for (const auto y : {centerY - 12, centerY + 12}) {
                    canvas.fillSmoothCircle(x, y, 7, chain_iot::view::draw::DevicePressedColor);
                }
            }
            break;
        case NodeKind::Mono:
        case NodeKind::Rgb: {
            if (kind == NodeKind::Rgb) {
                canvas.fillSmoothRoundRect(left, top, Size, Size, Radius, 0x3F3F3FU);
                canvas.setBaseColor(0x3F3F3FU);
            }
            const auto display_color = (static_cast<std::uint32_t>(color.red) << 16U) |
                                       (static_cast<std::uint32_t>(color.green) << 8U) | color.blue;
            for (const auto y : {top + 5, top + 26, top + 48}) {
                for (const auto x : {left + 5, left + 26, left + 48}) {
                    canvas.fillSmoothRoundRect(x, y, 19, 19, 2, display_color);
                }
            }
            break;
        }
        case NodeKind::Pir:
            canvas.fillSmoothCircle(centerX, centerY, 25, 0xA6A6A6U);
            canvas.fillSmoothCircle(centerX, centerY, 23, 0xFFFFFFU);
            break;
        case NodeKind::Tof:
            canvas.fillSmoothRoundRect(centerX - 26, centerY - 22, 53, 43, 12, 0xBCBCBCU);
            canvas.fillSmoothRoundRect(centerX - 12, centerY - 7, 24, 14, 3, 0x3D3D3DU);
            break;
        default:
            break;
    }
}

const char* buzzerModeLabel(std::uint8_t mode)
{
    switch (mode) {
        case 0:
            return "Auto";
        case 1:
            return "Manual";
        case 2:
            return "Note";
        default:
            return "Unknown";
    }
}

}  // namespace

namespace chain_iot::view {

RgbNode::RgbNode(std::uint32_t id, NodeKind kind, std::string_view title)
    : _id(id),
      _kind(kind),
      _title(title),
      _rgb_control(kind == NodeKind::Mono ? RgbControl::Palette::Grayscale : RgbControl::Palette::Rgb)
{
}

std::uint32_t RgbNode::id() const
{
    return _id;
}

NodeKind RgbNode::kind() const
{
    return _kind;
}

std::string_view RgbNode::title() const
{
    return _title;
}

bool RgbNode::detailAvailable() const
{
    return true;
}

DetailLayout RgbNode::detailLayout() const
{
    return DetailLayout::InfoPanel;
}

bool RgbNode::apply(const ChainSnapshot& snapshot)
{
    const auto* device = findChainDevice(snapshot, _id);
    if (device == nullptr) {
        return false;
    }

    const bool kind_matches = (_kind == NodeKind::Buzzer && device->kind == ChainDeviceKind::Buzzer) ||
                              (_kind == NodeKind::Encoder && device->kind == ChainDeviceKind::Encoder) ||
                              (_kind == NodeKind::Mono && device->kind == ChainDeviceKind::Mono) ||
                              (_kind == NodeKind::Rgb && device->kind == ChainDeviceKind::Rgb) ||
                              (_kind == NodeKind::Pir && device->kind == ChainDeviceKind::Pir) ||
                              (_kind == NodeKind::Tof && device->kind == ChainDeviceKind::Tof);
    if (!kind_matches) return false;

    bool changed      = false;
    const auto update = [&changed](auto& target, const auto& value) {
        if (target == value) return;
        target  = value;
        changed = true;
    };

    switch (_kind) {
        case NodeKind::Buzzer: {
            const auto* data = std::get_if<ChainBuzzerData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_buzzer_frequency, data->frequency);
            update(_buzzer_mode, data->mode);
            update(_buzzer_duty, data->duty);
            update(_buzzer_enabled, data->enabled);
            break;
        }
        case NodeKind::Encoder: {
            const auto* data = std::get_if<ChainEncoderData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_encoder_value, data->value);
            update(_encoder_delta, data->delta);
            update(_encoder_pressed, data->pressed);
            break;
        }
        case NodeKind::Mono: {
            const auto* data = std::get_if<ChainMonoData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_display_mode, data->mode);
            update(_brightness, data->brightness);
            break;
        }
        case NodeKind::Rgb: {
            const auto* data = std::get_if<ChainRgbData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_display_mode, data->mode);
            update(_brightness, data->brightness);
            break;
        }
        case NodeKind::Pir: {
            const auto* data = std::get_if<ChainPirData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_pir_detected, data->detected);
            break;
        }
        case NodeKind::Tof: {
            const auto* data = std::get_if<ChainTofData>(&device->data);
            if (data == nullptr) return false;
            changed |= _rgb_control.setColor(data->rgb);
            update(_distance_mm, data->distanceMm);
            break;
        }
        default:
            return false;
    }
    return changed;
}

void RgbNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    switch (_kind) {
        case NodeKind::Buzzer:
            _icon_cache.prepare(canvas, renderBuzzerIcon);
            break;
        case NodeKind::Encoder:
            _icon_cache.prepare(canvas, renderEncoderIcon);
            break;
        case NodeKind::Mono:
            _icon_cache.prepare(canvas, renderMonoIcon);
            break;
        case NodeKind::Rgb:
            _icon_cache.prepare(canvas, renderRgbIcon);
            break;
        case NodeKind::Pir:
            _icon_cache.prepare(canvas, renderPirIcon);
            break;
        case NodeKind::Tof:
            _icon_cache.prepare(canvas, renderTofIcon);
            break;
        default:
            break;
    }
}

void RgbNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    switch (_kind) {
        case NodeKind::Buzzer:
            _icon_cache.draw(canvas, centerX, centerY, renderBuzzerIcon);
            break;
        case NodeKind::Encoder:
            _icon_cache.draw(canvas, centerX, centerY, renderEncoderIcon);
            break;
        case NodeKind::Mono:
            _icon_cache.draw(canvas, centerX, centerY, renderMonoIcon);
            break;
        case NodeKind::Rgb:
            _icon_cache.draw(canvas, centerX, centerY, renderRgbIcon);
            break;
        case NodeKind::Pir:
            _icon_cache.draw(canvas, centerX, centerY, renderPirIcon);
            break;
        case NodeKind::Tof:
            _icon_cache.draw(canvas, centerX, centerY, renderTofIcon);
            break;
        default:
            break;
    }
}

void RgbNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, _title, detailAvailable());
    char line[48];
    const auto rgb = _rgb_control.color();
    switch (_kind) {
        case NodeKind::Buzzer:
            std::snprintf(line, sizeof(line), "Freq: %u Hz  Duty: %u%%", static_cast<unsigned>(_buzzer_frequency),
                          static_cast<unsigned>(_buzzer_duty));
            draw::drawInfoLine(canvas, panelY, 0, line);
            std::snprintf(line, sizeof(line), "Mode: %s %s  #%02X%02X%02X", buzzerModeLabel(_buzzer_mode),
                          _buzzer_enabled ? "On" : "Off", rgb.red, rgb.green, rgb.blue);
            draw::drawInfoLine(canvas, panelY, 1, line);
            break;
        case NodeKind::Encoder:
            std::snprintf(line, sizeof(line), "Value: %d  Delta: %+d", static_cast<int>(_encoder_value),
                          static_cast<int>(_encoder_delta));
            draw::drawInfoLine(canvas, panelY, 0, line);
            std::snprintf(line, sizeof(line), "Key: %s  #%02X%02X%02X", _encoder_pressed ? "Pressed" : "Released",
                          rgb.red, rgb.green, rgb.blue);
            draw::drawInfoLine(canvas, panelY, 1, line);
            break;
        case NodeKind::Mono:
        case NodeKind::Rgb:
            std::snprintf(line, sizeof(line), "Mode: %u  Bright: %u", static_cast<unsigned>(_display_mode),
                          static_cast<unsigned>(_brightness));
            draw::drawInfoLine(canvas, panelY, 0, line);
            std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
            draw::drawInfoLine(canvas, panelY, 1, line);
            break;
        case NodeKind::Pir:
            draw::drawInfoLine(canvas, panelY, 0, _pir_detected ? "Motion: Detected" : "Motion: Idle");
            std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
            draw::drawInfoLine(canvas, panelY, 1, line);
            break;
        case NodeKind::Tof:
            std::snprintf(line, sizeof(line), "Distance: %u mm", static_cast<unsigned>(_distance_mm));
            draw::drawInfoLine(canvas, panelY, 0, line);
            std::snprintf(line, sizeof(line), "RGB: #%02X%02X%02X", rgb.red, rgb.green, rgb.blue);
            draw::drawInfoLine(canvas, panelY, 1, line);
            break;
        default:
            break;
    }
}

bool RgbNode::handleDetailInput(input::KeyEvent event)
{
    const auto before  = _rgb_control.color();
    const bool handled = _rgb_control.handleInput(event);
    const auto after   = _rgb_control.color();
    if (before.red != after.red || before.green != after.green || before.blue != after.blue) {
        GetHAL().setChainRgb(_id, after);
    }
    return handled;
}

bool RgbNode::updateDetail(std::uint32_t currentTime)
{
    return _rgb_control.update(currentTime);
}

void RgbNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    const int card_y = frame.y + 14;
    const int card_x = frame.x + 15 - static_cast<int>(std::lround(112.0F * _rgb_control.expansion()));

    canvas.setBaseColor(draw::InfoPanelColor);
    const auto card_color = _kind == NodeKind::Rgb ? 0x3F3F3FU : draw::TileColor;
    canvas.fillSmoothRoundRect(card_x, card_y, DetailCardWidth, DetailCardHeight, DetailCardRadius, card_color);
    canvas.setBaseColor(card_color);
    drawLargeIcon(canvas, _kind, card_x + DetailCardWidth / 2, card_y + DetailCardHeight / 2, _rgb_control.color());

    _rgb_control.draw(canvas, {frame.x + 133, card_y, DetailCardWidth, DetailCardHeight},
                      {frame.x + 15, card_y, frame.width - 30, DetailCardHeight});
}

}  // namespace chain_iot::view
