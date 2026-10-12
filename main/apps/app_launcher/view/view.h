/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <apps/common/status_bar/status_bar.h>
#include <cstdint>
#include <functional>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <vector>

namespace view {

class LauncherView : public uitk::SmoothSelectorMenu {
public:
    ~LauncherView() override;

    std::function<void(int appId)> onAppClicked;

    void init(std::vector<mooncake::AppProps_t> appProps);
    void destroy();
    void handleInput(input::KeyEvent event);
    void redraw();

private:
    enum class PanelPhase {
        Showing,
        Visible,
        Hiding,
        Waiting,
    };

    enum class NavigationInputState {
        Ready,
        Animating,
        AwaitingNeutral,
    };

    struct RenderState {
        int contentX                = 0;
        int contentY                = 0;
        int selectorX               = 0;
        int selectorY               = 0;
        int selectorWidth           = 1;
        int selectorHeight          = 1;
        int panelY                  = 0;
        int indicatorIndex          = 0;
        std::uint32_t selectorColor = 0;
        bool nameVisible            = true;
        bool selectorForeground     = false;

        bool operator==(const RenderState& other) const
        {
            return contentX == other.contentX && contentY == other.contentY && selectorX == other.selectorX &&
                   selectorY == other.selectorY && selectorWidth == other.selectorWidth &&
                   selectorHeight == other.selectorHeight && panelY == other.panelY &&
                   indicatorIndex == other.indicatorIndex && selectorColor == other.selectorColor &&
                   nameVisible == other.nameVisible && selectorForeground == other.selectorForeground;
        }
    };

    std::vector<mooncake::AppProps_t> _app_props;
    uitk::AnimateValue _panel_y;
    uitk::AnimateValue _selector_motion;
    uitk::color::AnimateRgb_t _selector_color;

    StatusBar _status_bar;
    PanelPhase _panel_phase                      = PanelPhase::Showing;
    std::uint32_t _return_at                     = 0;
    std::uint32_t _last_motion_tick              = 0;
    int _panel_selected_index                    = 0;
    float _last_selector_x                       = 0.0F;
    bool _name_visible                           = true;
    bool _selector_foreground                    = false;
    bool _motion_sample_valid                    = false;
    bool _is_restoring                           = false;
    int _held_edge_direction                     = 0;
    NavigationInputState _navigation_input_state = NavigationInputState::Ready;
    bool _resume_held_navigation                 = false;
    std::uint8_t _neutral_input_polls            = 0;
    RenderState _render_state;
    bool _render_state_valid = false;
    bool _render_requested   = true;

    void configureAnimations();
    void beginPanelHide();
    void beginPanelReturn();
    void updatePanel(std::uint32_t currentTime);
    bool atNavigationEdge(int direction);
    void goHome();
    bool blockNavigationInput(input::KeyEvent event);
    bool navigationAnimationArrived();
    void beginNavigationInputLock(bool resumeHeldNavigation = false);
    std::uint32_t selectedThemeColor() const;
    void draw() const;

    void onRender() override;
    void onUpdate(std::uint32_t currentTime) override;
    void onGoLast() override;
    void onGoNext() override;
    void onClick() override;
    void onOpenEnd() override;
    void _update_camera_keyframe() override;
};

}  // namespace view
