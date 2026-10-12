/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../../theme.h"

#include <cstdint>
#include <string_view>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace chain_iot::view::draw {

inline constexpr std::uint32_t MenuBackgroundColor = chain_iot::ThemeColor;
inline constexpr std::uint32_t TileColor           = 0xD9D9D9;
inline constexpr std::uint32_t DeviceColor         = 0xF64E72;
inline constexpr std::uint32_t DeviceBorderColor   = 0xECADBB;
inline constexpr std::uint32_t AngleBorderColor    = 0xFC7693;
inline constexpr std::uint32_t DevicePressedColor  = 0xD63256;
inline constexpr std::uint32_t CaptainBodyColor    = 0x454444;
inline constexpr std::uint32_t CaptainPortColor    = 0xA2A2A2;
inline constexpr std::uint32_t CaptainPowerColor   = 0xEE7E7E;
inline constexpr std::uint32_t ConnectionColor     = 0x767088;
inline constexpr std::uint32_t InfoPanelColor      = 0xF0F4F6;
inline constexpr std::uint32_t MenuSurfaceColor    = 0x5B5768;

void drawInfoChrome(lgfx::LGFX_Sprite& canvas, int panelY, std::string_view title, bool showActionHint);
void drawInfoLine(lgfx::LGFX_Sprite& canvas, int panelY, int line, const char* text);

}  // namespace chain_iot::view::draw
