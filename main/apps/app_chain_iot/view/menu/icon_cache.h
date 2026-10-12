/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <memory>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace chain_iot::view {

class OpaqueIconCache final {
public:
    using Renderer = void (*)(lgfx::LGFX_Sprite& canvas, int centerX, int centerY);

    OpaqueIconCache(int width, int height);
    ~OpaqueIconCache();

    OpaqueIconCache(const OpaqueIconCache&)            = delete;
    OpaqueIconCache& operator=(const OpaqueIconCache&) = delete;

    void prepare(lgfx::LGFX_Sprite& parent, Renderer renderer) const;
    void draw(lgfx::LGFX_Sprite& parent, int centerX, int centerY, Renderer renderer) const;
    void invalidate();

private:
    int _width  = 0;
    int _height = 0;
    mutable std::unique_ptr<lgfx::LGFX_Sprite> _sprite;
    mutable bool _dirty = true;
};

}  // namespace chain_iot::view
