/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../chain_node.h"
#include "../icon_cache.h"
#include "../../detail/rgb_control.h"

namespace chain_iot::view {

class KeyNode final : public ChainNode {
public:
    explicit KeyNode(std::uint32_t id);

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
    bool _pressed     = false;
    RgbControl _rgb_control;
    mutable OpaqueIconCache _icon_cache{30, 30};
};

}  // namespace chain_iot::view
