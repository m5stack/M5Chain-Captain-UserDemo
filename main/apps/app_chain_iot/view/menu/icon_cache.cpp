/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "icon_cache.h"

#include "node_draw.h"

#include <cstdio>
#include <M5GFX.h>

namespace chain_iot::view {

OpaqueIconCache::OpaqueIconCache(int width, int height) : _width(width), _height(height)
{
}

OpaqueIconCache::~OpaqueIconCache() = default;

void OpaqueIconCache::prepare(lgfx::LGFX_Sprite& parent, Renderer renderer) const
{
    if (!_dirty && _sprite != nullptr) return;

    if (_sprite == nullptr) {
        auto sprite = std::make_unique<lgfx::LGFX_Sprite>(&parent);
        sprite->setColorDepth(parent.getColorDepth());
        sprite->setPsram(true);
        if (sprite->createSprite(_width, _height) == nullptr) return;
        _sprite = std::move(sprite);
    }

    _sprite->fillScreen(draw::MenuSurfaceColor);
    _sprite->fillRect(0, _height / 2 - 1, _width, 2, draw::ConnectionColor);
    renderer(*_sprite, _width / 2, _height / 2);
    _dirty = false;
}

void OpaqueIconCache::draw(lgfx::LGFX_Sprite& parent, int centerX, int centerY, Renderer renderer) const
{
    prepare(parent, renderer);
    if (_sprite != nullptr && !_dirty) {
        _sprite->pushSprite(centerX - _width / 2, centerY - _height / 2);
        return;
    }
    renderer(parent, centerX, centerY);
}

void OpaqueIconCache::invalidate()
{
    _dirty = true;
}

}  // namespace chain_iot::view
