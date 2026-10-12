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

class VerticalLevelView final : public LevelModeView {
public:
    ~VerticalLevelView() override;

    void init() override;
    void update(std::uint32_t currentTime) override;
    void draw(lgfx::LGFX_Sprite& canvas) override;
    void setImuData(const ImuData& data) override;
    void setDataLabelsVisible(bool visible) override;
    bool renderRequested() const override;
    void clearRenderRequest() override;
    void startEnterTransition();
    LevelSurfaceTransition surfaceTransition() const;

private:
    void destroy();
    void applyOrientation(std::uint32_t currentTime, bool force = false);
    void applyPalette(std::uint32_t currentTime, bool force = false);
    void updateAngleSource(int displayAngle);
    void updateStability(const ImuData& data, float gyroMotion, float angle);
    void setStable(bool stable);

    uitk::AnimateValue _angle;
    uitk::AnimateValue _orientation;
    uitk::AnimateValue _screen_orientation;
    uitk::AnimateValue _transition_y;
    uitk::color::AnimateRgb_t _background_color;
    uitk::color::AnimateRgb_t _surface_color;
    uitk::color::AnimateRgb_t _tick_color;
    uitk::color::AnimateRgb_t _label_color;
    bool _has_imu_data                             = false;
    bool _orientation_animating                    = false;
    bool _is_stable                                = false;
    bool _stability_pending                        = false;
    bool _palette_animating                        = false;
    bool _baseline_initialized                     = false;
    bool _transition_animating                     = false;
    bool _render_requested                         = true;
    std::uint32_t _stable_since                    = 0;
    std::uint32_t _last_orientation_render         = 0;
    std::uint32_t _last_palette_render             = 0;
    float _reading_baseline                        = 0.0F;
    float _rendered_orientation                    = 0.0F;
    float _rendered_screen_orientation             = 0.0F;
    float _rendered_transition_y                   = 0.0F;
    int _displayed_angle                           = 361;
    std::array<std::uint8_t, 180 * 60> _angle_mask = {};
    std::int16_t _angle_mask_left                  = 0;
    std::int16_t _angle_mask_top                   = 0;
    std::int16_t _angle_mask_right                 = 0;
    std::int16_t _angle_mask_bottom                = 0;
};

}  // namespace view
