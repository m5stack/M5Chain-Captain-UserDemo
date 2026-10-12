/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../chain_node.h"
#include "../icon_cache.h"
#include "../../detail/rgb_control.h"

#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace chain_iot::view {

class AngleNode final : public ChainNode {
public:
    explicit AngleNode(std::uint32_t id);

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
    std::uint32_t _id    = 0;
    std::uint16_t _angle = 0;
    uitk::AnimateValue _angle_animation;
    float _display_angle             = 0.0F;
    std::uint32_t _last_angle_update = 0;
    bool _angle_initialized          = false;
    RgbControl _rgb_control;
    mutable OpaqueIconCache _icon_cache{30, 30};
};

}  // namespace chain_iot::view
