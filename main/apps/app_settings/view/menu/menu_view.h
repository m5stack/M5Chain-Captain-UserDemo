/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "menu_item.h"

#include <apps/common/key_manager/key_manager.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <smooth_ui_toolkit.hpp>
#include <string_view>
#include <type_traits>
#include <uitk/short_namespace.hpp>
#include <utility>
#include <vector>

namespace settings::view {

struct MenuSection {
    explicit MenuSection(std::string_view sectionTitle) : title(sectionTitle)
    {
    }

    MenuSection(MenuSection&&) noexcept            = default;
    MenuSection& operator=(MenuSection&&) noexcept = default;
    MenuSection(const MenuSection&)                = delete;
    MenuSection& operator=(const MenuSection&)     = delete;

    template <typename Item, typename... Args>
    Item& addItem(Args&&... args)
    {
        static_assert(std::is_base_of_v<MenuItem, Item>);
        auto item   = std::make_unique<Item>(std::forward<Args>(args)...);
        auto& value = *item;
        items.push_back(std::move(item));
        return value;
    }

    std::string_view title;
    std::vector<std::unique_ptr<MenuItem>> items;
};

class MenuView : public uitk::SmoothSelectorMenu {
public:
    using OverlayDrawCallback = std::function<void(lgfx::LGFX_Sprite& canvas)>;

    ~MenuView() override;

    void init(std::vector<MenuSection> sections, bool entryFade = true, bool beginEditing = false,
              int contentOffsetY = 0);
    void destroy();
    // Embedded menus leave drawing and presentation to their owning view.
    void setEmbedded(bool embedded);
    bool takeRenderRequest();
    void draw(lgfx::LGFX_Sprite& canvas) const;
    void setTheme(const Theme& theme);
    void setUnderlayDrawCallback(OverlayDrawCallback callback);
    void setOverlayDrawCallback(OverlayDrawCallback callback);

    bool handleInput(input::KeyEvent event);
    bool editing() const;
    bool transitioning() const;
    void redraw();
    void resume();
    void transitionToPage(std::function<void()> onComplete);

private:
    enum class InteractionState {
        Navigate,
        Edit,
    };

    enum class PageTransitionPhase {
        Inactive,
        Opening,
        Returning,
    };

    struct SectionLayout {
        std::string_view title;
        int y = 0;
    };

    struct ItemLayout {
        MenuItem* item = nullptr;
    };

    struct RenderState {
        int cameraX                 = 0;
        int cameraY                 = 0;
        int selectorX               = 0;
        int selectorY               = 0;
        int selectorWidth           = 1;
        int selectorHeight          = 1;
        std::uint32_t selectorColor = 0;
        int selectedIndex           = 0;
        bool selectorForeground     = false;

        bool operator==(const RenderState& other) const;
    };

    MenuItem* selectedItem();
    const MenuItem* selectedItem() const;
    void buildLayout(int contentOffsetY);
    void leaveEdit(bool commit);
    void configureSelectorAnimations();
    void configurePressAnimations();
    void configureReleaseAnimations();
    void configurePageTransitionAnimations();
    bool atNavigationEdge(int direction);

    void onUpdate(std::uint32_t currentTime) override;
    void onRender() override;
    void onRelease() override;
    void onClick() override;
    void onOpenEnd() override;
    void _update_camera_keyframe() override;

    std::vector<MenuSection> _sections;
    std::vector<SectionLayout> _section_layouts;
    std::vector<ItemLayout> _item_layouts;
    Theme _theme = DefaultTheme;
    OverlayDrawCallback _overlay_draw_callback;
    OverlayDrawCallback _underlay_draw_callback;
    RenderState _render_state;
    uitk::AnimateValue _selector_motion;
    uitk::AnimateValue _selector_transition_mix;
    uitk::AnimateValue _edit_mix;
    uitk::AnimateValue _entry_mask_opacity;
    std::function<void()> _transition_out_complete;
    float _last_selector_y                     = 0.0F;
    float _last_edit_mix                       = 0.0F;
    float _last_entry_mask_opacity             = 0.0F;
    std::uint32_t _last_motion_tick            = 0;
    int _edit_visual_item_index                = -1;
    int _held_edge_direction                   = 0;
    bool _render_state_valid                   = false;
    bool _render_requested                     = true;
    bool _embedded                             = false;
    bool _embedded_render_pending              = true;
    bool _motion_sample_valid                  = false;
    bool _selector_foreground                  = false;
    InteractionState _interaction              = InteractionState::Navigate;
    PageTransitionPhase _page_transition_phase = PageTransitionPhase::Inactive;
};

}  // namespace settings::view
