/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstdint>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace view {

enum class PomodoroPanel : std::uint8_t {
    None = 0,
    Pomodoro,
    ShortBreak,
    LongBreak,
};

class PomodoroTimerView {
public:
    ~PomodoroTimerView();

    void init();
    void destroy();
    void update(std::uint32_t currentTime);
    void setActivePanel(PomodoroPanel panel);
    void setIdleTilt(float x, float y);
    void setRemainingSeconds(PomodoroPanel panel, std::uint32_t seconds);
    void setTimerFinished(PomodoroPanel panel, bool finished);

private:
    struct PanelVisual {
        float x            = 0.0F;
        float y            = 0.0F;
        float width        = 0.0F;
        float height       = 0.0F;
        float radius       = 0.0F;
        float labelCenterX = 0.0F;
        float labelCenterY = 0.0F;
        float activeMix    = 0.0F;
    };

    struct PanelAnimation {
        uitk::AnimateValue x;
        uitk::AnimateValue y;
        uitk::AnimateValue width;
        uitk::AnimateValue height;
        uitk::AnimateValue radius;
        uitk::AnimateValue labelCenterX;
        uitk::AnimateValue labelCenterY;
        uitk::AnimateValue activeMix;
    };

    using Layout = std::array<PanelVisual, 3>;

    static Layout idleLayout();
    static Layout layoutFor(PomodoroPanel panel);
    static void drawRectangle(lgfx::LGFX_Sprite& canvas, const PanelVisual& panel, std::uint32_t color);
    static void preparePanelAnimation(PanelAnimation& animation);
    static void teleportPanel(PanelAnimation& animation, const PanelVisual& panel);
    static void movePanel(PanelAnimation& animation, const PanelVisual& panel);
    static PanelVisual samplePanel(PanelAnimation& animation);
    static PanelVisual applyEntryOffset(PanelVisual panel, std::size_t index, float offset);
    static PanelVisual applyIdleTilt(PanelVisual panel, std::size_t index, float tiltX, float tiltY);
    static bool panelAnimationDone(PanelAnimation& animation);

    void applyAnimations(std::uint32_t currentTime, bool force = false);
    void draw(lgfx::LGFX_Sprite& canvas);
    void renderIfRequested();

    std::array<PanelAnimation, 3> _panel_animations;
    Layout _rendered_layout;
    std::array<std::uint32_t, 3> _remaining_seconds = {25U * 60U, 5U * 60U, 15U * 60U};
    std::array<std::array<char, 16>, 3> _timer_text = {};
    std::array<bool, 3> _timer_finished             = {};
    uitk::AnimateValue _idle_tilt_x;
    uitk::AnimateValue _idle_tilt_y;
    uitk::AnimateValue _entry_offset;
    uitk::AnimateValue _instruction_entry_opacity;
    uitk::AnimateSequence _finished_icon_shake;
    PomodoroPanel _active_panel          = PomodoroPanel::None;
    std::uint32_t _last_layout_render    = 0;
    float _rendered_idle_tilt_x          = 0.0F;
    float _rendered_idle_tilt_y          = 0.0F;
    float _rendered_entry_offset         = 0.0F;
    float _rendered_instruction_opacity  = 0.0F;
    float _rendered_finished_icon_shake  = 0.0F;
    bool _layout_animating               = false;
    bool _idle_tilt_animating            = false;
    bool _has_finished_timer             = false;
    bool _instruction_transition_visible = true;
    bool _render_requested               = true;
};

}  // namespace view
