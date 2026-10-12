/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

// M5GFX's SDL DataWrapper uses FILE without including the stdio declarations.
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace screensaver {

class Screensaver {
public:
    virtual ~Screensaver()                                      = default;
    virtual void init(int width, int height, std::uint32_t now) = 0;
    // Return true only when a new frame needs presenting.
    virtual bool update(std::uint32_t now)             = 0;
    virtual void draw(lgfx::LGFX_Sprite& canvas) const = 0;
};

struct Registration {
    std::string_view id;
    std::string_view label;
    std::unique_ptr<Screensaver> (*create)();
};

// Stable IDs are saved in settings; both Settings and Launcher use this catalog.
const std::vector<Registration>& registry();
const Registration& find(std::string_view id);

}  // namespace screensaver
