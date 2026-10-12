/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../../detail/rgb_control.h"
#include "../chain_node.h"
#include "../icon_cache.h"

#include <cstdint>
#include <string_view>

namespace chain_iot::view {

class RgbNode final : public ChainNode {
public:
    RgbNode(std::uint32_t id, NodeKind kind, std::string_view title);

    std::uint32_t id() const override;
    NodeKind kind() const override;
    std::string_view title() const override;
    bool detailAvailable() const override;
    DetailLayout detailLayout() const override;
    bool apply(const ChainSnapshot& snapshot) override;
    void prepareIcon(lgfx::LGFX_Sprite& canvas) const override;
    void drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const override;
    void drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const override;
    bool handleDetailInput(input::KeyEvent event) override;
    bool updateDetail(std::uint32_t currentTime) override;
    void drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const override;

private:
    std::uint32_t _id = 0;
    NodeKind _kind    = NodeKind::Buzzer;
    std::string_view _title;
    RgbControl _rgb_control;
    mutable OpaqueIconCache _icon_cache{30, 30};

    std::uint16_t _buzzer_frequency = 0;
    std::uint8_t _buzzer_mode       = 0;
    std::uint8_t _buzzer_duty       = 0;
    bool _buzzer_enabled            = false;
    std::int16_t _encoder_value     = 0;
    std::int16_t _encoder_delta     = 0;
    bool _encoder_pressed           = false;
    std::uint8_t _display_mode      = 0;
    std::uint8_t _brightness        = 0;
    bool _pir_detected              = false;
    std::uint16_t _distance_mm      = 0;
};

}  // namespace chain_iot::view
