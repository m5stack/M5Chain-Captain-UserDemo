/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <hal/hal.h>
#include <memory>
#include <string_view>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace chain_iot::view {

struct Frame {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

enum class NodeKind : std::uint8_t {
    Empty,
    Captain,
    Key,
    Angle,
    Joystick,
    Buzzer,
    Encoder,
    Mono,
    Rgb,
    Pir,
    Tof,
};

enum class DetailLayout : std::uint8_t {
    FullScreen,
    InfoPanel,
};

class ChainNode {
public:
    virtual ~ChainNode() = default;

    virtual std::uint32_t id() const                  = 0;
    virtual NodeKind kind() const                     = 0;
    virtual std::string_view title() const            = 0;
    virtual bool detailAvailable() const              = 0;
    virtual DetailLayout detailLayout() const         = 0;
    virtual bool apply(const ChainSnapshot& snapshot) = 0;

    virtual void prepareIcon(lgfx::LGFX_Sprite& canvas) const                        = 0;
    virtual void drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const = 0;
    virtual void drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const               = 0;

    // Unconsumed Home events return from the detail page to the Chain menu.
    virtual bool handleDetailInput(input::KeyEvent event)                        = 0;
    virtual bool updateDetail(std::uint32_t currentTime)                         = 0;
    virtual void drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const = 0;
};

std::unique_ptr<ChainNode> makeEmptyNode(bool leftSide);
std::unique_ptr<ChainNode> makeCaptainNode();
std::unique_ptr<ChainNode> makeChainNode(const ChainDeviceSnapshot& device);
const ChainDeviceSnapshot* findChainDevice(const ChainSnapshot& snapshot, std::uint32_t id);

}  // namespace chain_iot::view
