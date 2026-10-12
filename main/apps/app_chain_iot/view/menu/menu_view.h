/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../detail/detail_view.h"
#include "chain_node.h"

#include <cstdint>
#include <hal/hal.h>
#include <memory>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <vector>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace chain_iot::view {

class ChainMenuView final {
public:
    static void drawLoading();
    void init(const ChainSnapshot& snapshot);
    void applySnapshot(const ChainSnapshot& snapshot);
    // Returns false only when Home should close the app.
    bool handleInput(input::KeyEvent event);
    void update(std::uint32_t currentTime);

private:
    struct RenderState {
        int contentOffset             = 0;
        int indicatorOffsetX          = 0;
        int infoOffsetY               = 0;
        int menuOffsetY               = 0;
        int detailMaskOpacity         = 255;
        int selectedIndex             = 0;
        DetailView::Phase detailPhase = DetailView::Phase::Hidden;

        bool operator==(const RenderState& other) const
        {
            return contentOffset == other.contentOffset && indicatorOffsetX == other.indicatorOffsetX &&
                   infoOffsetY == other.infoOffsetY && menuOffsetY == other.menuOffsetY &&
                   detailMaskOpacity == other.detailMaskOpacity && selectedIndex == other.selectedIndex &&
                   detailPhase == other.detailPhase;
        }
    };

    std::vector<std::unique_ptr<ChainNode>> _nodes;
    uitk::AnimateValue _content_offset;
    uitk::AnimateValue _info_offset_y;
    uitk::AnimateValue _menu_offset_y;
    uitk::AnimateValue _detail_mask_opacity;
    uitk::AnimateValue _wave_energy;
    uitk::AnimateValue _surface_tilt;
    DetailView _detail_view;
    std::uint32_t _topology_revision = 0;
    std::uint32_t _last_update_time  = 0;
    std::uint32_t _last_render_time  = 0;
    std::uint32_t _last_change_at    = 0;
    int _captain_index               = 0;
    int _selected_index              = 0;
    int _held_edge_direction         = 0;
    int _indicator_motion_direction  = 0;
    float _front_wave_phase          = 0.0F;
    float _back_wave_phase           = 0.0F;
    float _indicator_phase           = 0.0F;
    float _wave_boost_direction      = 1.0F;
    bool _has_update_time            = false;
    bool _wave_boost_requested       = false;
    bool _wave_boost_active          = false;
    bool _wave_boost_settling        = false;
    bool _indicator_motion_active    = false;
    bool _detail_fade_started        = false;
    bool _ok_armed                   = false;
    bool _render_requested           = true;
    bool _render_state_valid         = false;
    RenderState _render_state;

    void rebuildTopology(const ChainSnapshot& snapshot);
    void select(int index);
    void updateWaveMotion(std::uint32_t currentTime);
    int indicatorOffsetX();
    bool atNavigationEdge(int direction) const;
    bool navigationButtonsReleased() const;
    ChainNode* selectedNode();
    const ChainNode* renderedNode(int index) const;
    void render(std::uint32_t currentTime);
    void drawInfoLayer(lgfx::LGFX_Sprite& canvas, int offsetY) const;
    void drawInfoDetail(lgfx::LGFX_Sprite& canvas, const ChainNode& node) const;
    void drawFullScreenDetail(lgfx::LGFX_Sprite& canvas, const ChainNode& node) const;
    void drawMenuLayer(lgfx::LGFX_Sprite& canvas, int offsetY);
    void draw();
};

}  // namespace chain_iot::view
