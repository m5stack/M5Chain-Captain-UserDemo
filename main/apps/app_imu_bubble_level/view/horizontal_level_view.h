/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "mode_view.h"

#include <array>
#include <cstdint>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

class HorizontalLevelView final : public LevelModeView {
public:
    ~HorizontalLevelView() override;

    void init() override;
    void update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas) override;
    void setImuData(const ImuData& data) override;
    void setDataLabelsVisible(bool visible) override;
    bool renderRequested() const override;
    void clearRenderRequest() override;
    void startEntryFade();
    void startVerticalExitTransition(const LevelSurfaceTransition& transition);

private:
    void destroy();
    void applyBubbleState(std::uint32_t currentTime, bool force = false);
    void applyPalette(std::uint32_t currentTime, bool force = false);
    void applyEntryFade(std::uint32_t currentTime, bool force = false);
    void applyModeTransition(std::uint32_t currentTime, bool force = false);
    void setAccelLabelValues(float accelX, float accelY, float accelZ);
    void updateStability(const ImuData& data, float gyroMotion);
    void setStable(bool stable);

    uitk::AnimateValue _bubble_x;
    uitk::AnimateValue _bubble_y;
    uitk::AnimateValue _bubble_size;
    uitk::AnimateValue _entry_mask_opacity;
    uitk::AnimateValue _transition_offset;
    uitk::color::AnimateRgb_t _background_color;
    uitk::color::AnimateRgb_t _bubble_color;
    uitk::color::AnimateRgb_t _fixed_cross_color;
    uitk::color::AnimateRgb_t _label_color;
    std::array<char, 16> _accel_x_text    = {};
    std::array<char, 16> _accel_y_text    = {};
    std::array<char, 16> _accel_z_text    = {};
    bool _show_data_labels                = true;
    bool _has_imu_data                    = false;
    bool _bubble_animating                = false;
    bool _is_stable                       = false;
    bool _stability_pending               = false;
    bool _palette_animating               = false;
    bool _entry_fade_animating            = false;
    bool _transition_visible              = false;
    bool _transition_animating            = false;
    bool _render_requested                = true;
    std::uint32_t _stable_since           = 0;
    std::uint32_t _last_bubble_render     = 0;
    std::uint32_t _last_palette_render    = 0;
    std::uint32_t _last_entry_fade_render = 0;
    std::uint32_t _last_transition_render = 0;
    int _rendered_bubble_size             = 68;
    int _rendered_bubble_offset_x         = 0;
    int _rendered_bubble_offset_y         = 0;
    float _rendered_transition_offset     = 0.0F;
    std::uint8_t _rendered_mask_opacity   = 0;
    LevelSurfaceTransition _surface_transition;
};

}  // namespace view
