/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../pages/settings_page.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace settings::view {

class PageTransition {
public:
    enum class Direction {
        Push,
        Pop,
    };

    PageTransition();
    ~PageTransition();

    bool captureRoot();
    bool capturePage();
    void beginPush(TransitionSurface surface, std::uint32_t backgroundColor);
    void beginPop(std::uint32_t backgroundColor);
    std::optional<Direction> update(std::uint32_t currentTime);
    bool active() const;
    void reset();

private:
    bool ensureBuffers();
    void configureAnimation();
    void compose(float progress);
    void copyCanvasTo(lgfx::LGFX_Sprite& snapshot);

    std::unique_ptr<lgfx::LGFX_Sprite> _root_snapshot;
    std::unique_ptr<lgfx::LGFX_Sprite> _page_snapshot;
    uitk::AnimateValue _progress;
    Direction _direction                 = Direction::Push;
    TransitionSurface _page_surface      = TransitionSurface::Snapshot;
    std::uint32_t _page_background_color = 0xFFFFFF;
    std::uint32_t _last_render_time      = 0;
    bool _active                         = false;
    bool _buffers_ready                  = false;
};

}  // namespace settings::view
