/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "empty_node.h"

#include "../node_draw.h"

#include <M5GFX.h>

namespace {

void renderEmptyIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    canvas.fillSmoothCircle(centerX, centerY, 4, chain_iot::view::draw::ConnectionColor);
}

}  // namespace

namespace chain_iot::view {

EmptyNode::EmptyNode(bool leftSide) : _left_side(leftSide)
{
}

std::uint32_t EmptyNode::id() const
{
    return _left_side ? 0xFFFFFFFEU : 0xFFFFFFFFU;
}

NodeKind EmptyNode::kind() const
{
    return NodeKind::Empty;
}

std::string_view EmptyNode::title() const
{
    return "Empty";
}

bool EmptyNode::detailAvailable() const
{
    return false;
}

DetailLayout EmptyNode::detailLayout() const
{
    return DetailLayout::InfoPanel;
}

bool EmptyNode::apply(const ChainSnapshot& snapshot)
{
    (void)snapshot;
    return false;
}

void EmptyNode::prepareIcon(lgfx::LGFX_Sprite& canvas) const
{
    _icon_cache.prepare(canvas, renderEmptyIcon);
}

void EmptyNode::drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const
{
    _icon_cache.draw(canvas, centerX, centerY, renderEmptyIcon);
}

void EmptyNode::drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const
{
    draw::drawInfoChrome(canvas, panelY, title(), detailAvailable());
    draw::drawInfoLine(canvas, panelY, 0, "No Chain connected");
}

bool EmptyNode::handleDetailInput(input::KeyEvent event)
{
    (void)event;
    return false;
}

bool EmptyNode::updateDetail(std::uint32_t currentTime)
{
    (void)currentTime;
    return false;
}

void EmptyNode::drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const
{
    (void)canvas;
    (void)frame;
}

}  // namespace chain_iot::view
