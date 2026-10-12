/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "menu_view.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <cmath>
#include <hal/hal.h>
#include <utility>

using namespace uitk;

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;

constexpr int SectionTitleX      = 20;
constexpr int SectionTitleHeight = 17;
constexpr int FirstSectionY      = 14;
constexpr int TitleToItemOffset  = 31;
constexpr int ItemX              = 11;
constexpr int ItemWidth          = 218;
constexpr int ItemGap            = 14;
constexpr int FirstItemClearance = 4;

constexpr int SelectorInset      = 4;
constexpr int SelectorRadius     = 18;
constexpr int CameraTopMargin    = 41;
constexpr int CameraBottomMargin = 21;

constexpr float PressScale = 0.75F;

constexpr float SelectorPositionDuration    = 0.38F;
constexpr float SelectorShapeDuration       = 0.42F;
constexpr float SelectorBounce              = 0.24F;
constexpr float CameraDuration              = 0.55F;
constexpr float CameraBounce                = 0.16F;
constexpr float PressDuration               = 0.14F;
constexpr float PressBounce                 = 0.05F;
constexpr float PressRestDelta              = 0.5F;
constexpr float PressRestSpeed              = 5.0F;
constexpr float ReleaseDuration             = 0.2F;
constexpr float ReleaseBounce               = 0.08F;
constexpr float ReleaseRestDelta            = 0.75F;
constexpr float ReleaseRestSpeed            = 8.0F;
constexpr float DefaultRestDelta            = 0.1F;
constexpr float DefaultRestSpeed            = 0.1F;
constexpr float MotionEaseDuration          = 0.08F;
constexpr float MotionSpeedDeadZone         = 20.0F;
constexpr float MotionSpeedFullScale        = 300.0F;
constexpr float MotionWidthCompression      = 32.0F;
constexpr float MotionHeightCompression     = 6.0F;
constexpr float MotionColorFade             = 0.58F;
constexpr float EditColorDuration           = 0.18F;
constexpr float EntryFadeDuration           = 0.28F;
constexpr float PageTransitionDuration      = 0.35F;
constexpr float PageTransitionBounce        = 0.15F;
constexpr float PageTransitionRestDelta     = 2.0F;
constexpr float PageTransitionRestSpeed     = 15.0F;
constexpr float PageTransitionColorDuration = 0.28F;

const lgfx::LVGLFont SectionFont{&font_noto_sans_semibold_16};

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

bool isVisible(int x, int y, int width, int height)
{
    return x < ScreenWidth && x + width > 0 && y < ScreenHeight && y + height > 0;
}

bool navigationButtonsReleased()
{
    auto& hal = GetHAL();
    return !hal.btnB.isPressed() && !hal.btnC.isPressed();
}

void drawRectangle(lgfx::LGFX_Sprite& canvas, int x, int y, int width, int height, std::uint32_t color, int radius)
{
    if (!isVisible(x, y, width, height)) {
        return;
    }

    const int clamped_radius = std::max(0, std::min({radius, width / 2, height / 2}));
    if (clamped_radius == 0) {
        canvas.fillRect(x, y, width, height, color);
        return;
    }
    canvas.fillSmoothRoundRect(x, y, width, height, clamped_radius, color);
}

void configureSpring(AnimateValue& animation, float visualDuration, float bounce)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = visualDuration;
    options.bounce         = bounce;
    options.restDelta      = DefaultRestDelta;
    options.restSpeed      = DefaultRestSpeed;
    animation.delay        = 0.0F;
}

void configureSpring(AnimateVector2& animation, float visualDuration, float bounce)
{
    configureSpring(animation.x, visualDuration, bounce);
    configureSpring(animation.y, visualDuration, bounce);
}

void setSpringResponse(AnimateVector2& animation, float visualDuration, float bounce, float restDelta, float restSpeed)
{
    for (auto* value : {&animation.x, &animation.y}) {
        auto& options          = value->springOptions();
        options.duration       = 0.0F;
        options.visualDuration = visualDuration;
        options.bounce         = bounce;
        options.restDelta      = restDelta;
        options.restSpeed      = restSpeed;
        value->delay           = 0.0F;
    }
}

}  // namespace

namespace settings::view {

bool MenuView::RenderState::operator==(const RenderState& other) const
{
    return cameraX == other.cameraX && cameraY == other.cameraY && selectorX == other.selectorX &&
           selectorY == other.selectorY && selectorWidth == other.selectorWidth &&
           selectorHeight == other.selectorHeight && selectorColor == other.selectorColor &&
           selectedIndex == other.selectedIndex && selectorForeground == other.selectorForeground;
}

MenuView::~MenuView()
{
    destroy();
}

void MenuView::init(std::vector<MenuSection> sections, bool entryFade, bool beginEditing, int contentOffsetY)
{
    destroy();
    _sections = std::move(sections);

    setConfig().moveInLoop     = true;
    setConfig().renderInterval = 16;
    setCameraSize(ScreenWidth, ScreenHeight);
    buildLayout(contentOffsetY);
    if (_item_layouts.empty()) {
        return;
    }

    configureSelectorAnimations();
    auto& motion_options          = _selector_motion.easingOptions();
    motion_options.duration       = MotionEaseDuration;
    motion_options.easingFunction = ease::linear;
    _selector_motion.teleport(0.0F);

    auto& transition_options          = _selector_transition_mix.easingOptions();
    transition_options.duration       = PageTransitionColorDuration;
    transition_options.easingFunction = ease::ease_out_quad;
    _selector_transition_mix.teleport(0.0F);

    auto& edit_options          = _edit_mix.easingOptions();
    edit_options.duration       = EditColorDuration;
    edit_options.easingFunction = ease::ease_out_quad;
    _edit_mix.delay             = 0.0F;
    _edit_mix.teleport(0.0F);

    if (entryFade) {
        auto& entry_options          = _entry_mask_opacity.easingOptions();
        entry_options.duration       = EntryFadeDuration;
        entry_options.easingFunction = ease::ease_out_quad;
        _entry_mask_opacity.delay    = 0.0F;
        _entry_mask_opacity.teleport(255.0F);
        _entry_mask_opacity      = 0.0F;
        _last_entry_mask_opacity = 255.0F;
    } else {
        _entry_mask_opacity.teleport(0.0F);
        _last_entry_mask_opacity = 0.0F;
    }

    const auto& initial_selector = getSelectedKeyframe();
    getSelectorPostion().teleport(initial_selector.x, initial_selector.y);
    getSelectorShape().teleport(initial_selector.width, initial_selector.height);
    getCamera().teleport(0.0F, 0.0F);

    if (beginEditing) {
        auto* item = selectedItem();
        if (item != nullptr && item->activate() == ItemActivation::BeginEdit) {
            item->beginEdit();
            _interaction            = InteractionState::Edit;
            _edit_visual_item_index = _data.selected_option_index;
            _edit_mix.teleport(1.0F);
            _last_edit_mix = 1.0F;
        }
    }
    onRender();
}

void MenuView::destroy()
{
    _item_layouts.clear();
    _section_layouts.clear();
    _sections.clear();
    _render_state            = {};
    _render_state_valid      = false;
    _render_requested        = true;
    _last_selector_y         = 0.0F;
    _last_edit_mix           = 0.0F;
    _last_entry_mask_opacity = 0.0F;
    _last_motion_tick        = 0;
    _edit_visual_item_index  = -1;
    _held_edge_direction     = 0;
    _motion_sample_valid     = false;
    _selector_foreground     = false;
    _interaction             = InteractionState::Navigate;
    _page_transition_phase   = PageTransitionPhase::Inactive;
    _transition_out_complete = nullptr;
    _overlay_draw_callback   = nullptr;
    _underlay_draw_callback  = nullptr;
    _entry_mask_opacity.pause();

    _data.option_list.clear();
    _data.selected_option_index = 0;
    _data.read_input_time_count = 0;
    _data.render_time_count     = 0;
    _data.is_changed            = true;
    _data.is_pressing           = false;
    _data.was_released          = false;
    _data.is_opening            = false;
    _data.was_opened            = false;
}

void MenuView::setTheme(const Theme& theme)
{
    _theme            = theme;
    _render_requested = true;
}

void MenuView::setUnderlayDrawCallback(OverlayDrawCallback callback)
{
    _underlay_draw_callback = std::move(callback);
    _render_requested       = true;
}

void MenuView::setOverlayDrawCallback(OverlayDrawCallback callback)
{
    _overlay_draw_callback = std::move(callback);
    _render_requested      = true;
}

bool MenuView::handleInput(input::KeyEvent event)
{
    if (_item_layouts.empty()) {
        return false;
    }
    if (_held_edge_direction != 0 && navigationButtonsReleased()) {
        _held_edge_direction = 0;
    }
    if (_page_transition_phase != PageTransitionPhase::Inactive) {
        return true;
    }

    switch (event) {
        case input::KeyEvent::Home:
            audio::play_ui_sfx(audio::UiSfx::Back);
            if (editing()) {
                leaveEdit(false);
                return true;
            }
            return false;
        case input::KeyEvent::Previous:
        case input::KeyEvent::Next:
        case input::KeyEvent::PreviousRepeat:
        case input::KeyEvent::NextRepeat:
            if (isPressing()) {
                return true;
            }
            if (editing()) {
                const int direction =
                    event == input::KeyEvent::Previous || event == input::KeyEvent::PreviousRepeat ? -1 : 1;
                if (auto* item = selectedItem(); item != nullptr && item->adjust(direction)) {
                    _held_edge_direction = 0;
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                    _render_requested = true;
                } else if (_held_edge_direction == 0) {
                    _held_edge_direction = direction;
                    audio::play_ui_sfx(audio::UiSfx::Boundary);
                }
            } else {
                const bool repeated = event == input::KeyEvent::PreviousRepeat || event == input::KeyEvent::NextRepeat;
                const int direction =
                    event == input::KeyEvent::Previous || event == input::KeyEvent::PreviousRepeat ? -1 : 1;
                if (repeated && (_held_edge_direction != 0 || atNavigationEdge(direction))) {
                    if (_held_edge_direction == 0) {
                        _held_edge_direction = direction;
                        audio::play_ui_sfx(audio::UiSfx::Boundary);
                    }
                } else if (direction < 0) {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                    goLast();
                } else {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                    goNext();
                }
            }
            return true;
        case input::KeyEvent::OkPressed:
            if (const auto* item = selectedItem(); item != nullptr && !item->enabled()) {
                audio::play_ui_sfx(audio::UiSfx::Boundary);
                return true;
            }
            if (!isPressing()) {
                audio::play_ui_sfx(audio::UiSfx::Confirm);
                configurePressAnimations();
                const auto& selected       = getSelectedKeyframe();
                const float pressed_width  = selected.width * PressScale;
                const float pressed_height = selected.height * PressScale;
                press({selected.x + (selected.width - pressed_width) / 2.0F,
                       selected.y + (selected.height - pressed_height) / 2.0F, pressed_width, pressed_height});
            }
            return true;
        case input::KeyEvent::OkReleased:
            if (isPressing()) {
                release();
            }
            return true;
        default:
            return false;
    }
}

bool MenuView::atNavigationEdge(int direction)
{
    if (_item_layouts.empty()) {
        return true;
    }
    return direction < 0 ? getSelectedOptionIndex() == 0
                         : getSelectedOptionIndex() == static_cast<int>(_item_layouts.size()) - 1;
}

bool MenuView::transitioning() const
{
    return _page_transition_phase != PageTransitionPhase::Inactive;
}

bool MenuView::editing() const
{
    return _interaction == InteractionState::Edit;
}

void MenuView::redraw()
{
    _render_requested = true;
    onRender();
}

void MenuView::resume()
{
    configurePageTransitionAnimations();
    _selector_transition_mix.teleport(1.0F);
    _selector_transition_mix = 0.0F;
    _selector_foreground     = false;
    _page_transition_phase   = PageTransitionPhase::Returning;
    close();

    _render_requested    = true;
    _last_selector_y     = getSelectorPostion().y.directValue();
    _last_motion_tick    = GetHAL().millis();
    _motion_sample_valid = true;
    onRender();
}

void MenuView::transitionToPage(std::function<void()> onComplete)
{
    if (_page_transition_phase != PageTransitionPhase::Inactive || editing()) {
        return;
    }

    _transition_out_complete = std::move(onComplete);
    configurePageTransitionAnimations();
    getCamera().x.pause();
    getCamera().y.pause();

    const auto camera = getCamera().directValue();
    open({camera.x, camera.y, static_cast<float>(ScreenWidth), static_cast<float>(ScreenHeight)});
    _selector_transition_mix.teleport(0.0F);
    _selector_transition_mix = 1.0F;
    _selector_motion.teleport(0.0F);
    _selector_foreground   = true;
    _page_transition_phase = PageTransitionPhase::Opening;
    _render_requested      = true;
}

MenuItem* MenuView::selectedItem()
{
    const int index = _data.selected_option_index;
    if (index < 0 || index >= static_cast<int>(_item_layouts.size())) {
        return nullptr;
    }
    return _item_layouts[static_cast<std::size_t>(index)].item;
}

const MenuItem* MenuView::selectedItem() const
{
    const int index = _data.selected_option_index;
    if (index < 0 || index >= static_cast<int>(_item_layouts.size())) {
        return nullptr;
    }
    return _item_layouts[static_cast<std::size_t>(index)].item;
}

void MenuView::buildLayout(int contentOffsetY)
{
    int cursor_y            = FirstSectionY + contentOffsetY;
    std::size_t global_item = 0;
    for (auto& section : _sections) {
        _section_layouts.push_back({section.title, cursor_y});
        cursor_y += TitleToItemOffset;

        for (auto& item : section.items) {
            const Frame frame = {ItemX, cursor_y, ItemWidth, item->preferredHeight()};
            item->setFrame(frame);
            addOption({{static_cast<float>(frame.x - SelectorInset), static_cast<float>(frame.y - SelectorInset),
                        static_cast<float>(frame.width + SelectorInset * 2),
                        static_cast<float>(frame.height + SelectorInset * 2)},
                       nullptr});
            _item_layouts.push_back({item.get()});

            cursor_y += frame.height + ItemGap;
            if (global_item == 0) {
                cursor_y += FirstItemClearance;
            }
            ++global_item;
        }
    }
}

void MenuView::leaveEdit(bool commit)
{
    if (auto* item = selectedItem(); item != nullptr) {
        if (commit) {
            item->commitEdit();
        } else {
            item->cancelEdit();
        }
    }

    _interaction      = InteractionState::Navigate;
    _edit_mix         = 0.0F;
    _render_requested = true;
}

void MenuView::configureSelectorAnimations()
{
    configureSpring(getSelectorPostion(), SelectorPositionDuration, SelectorBounce);
    configureSpring(getSelectorShape(), SelectorShapeDuration, SelectorBounce);
    configureSpring(getCamera(), CameraDuration, CameraBounce);
}

void MenuView::configurePressAnimations()
{
    setSpringResponse(getSelectorPostion(), PressDuration, PressBounce, PressRestDelta, PressRestSpeed);
    setSpringResponse(getSelectorShape(), PressDuration, PressBounce, PressRestDelta, PressRestSpeed);
}

void MenuView::configureReleaseAnimations()
{
    setSpringResponse(getSelectorPostion(), ReleaseDuration, ReleaseBounce, ReleaseRestDelta, ReleaseRestSpeed);
    setSpringResponse(getSelectorShape(), ReleaseDuration, ReleaseBounce, ReleaseRestDelta, ReleaseRestSpeed);
}

void MenuView::configurePageTransitionAnimations()
{
    setSpringResponse(getSelectorPostion(), PageTransitionDuration, PageTransitionBounce, PageTransitionRestDelta,
                      PageTransitionRestSpeed);
    setSpringResponse(getSelectorShape(), PageTransitionDuration, PageTransitionBounce, PageTransitionRestDelta,
                      PageTransitionRestSpeed);
}

void MenuView::onUpdate(std::uint32_t currentTime)
{
    const float current_time = static_cast<float>(currentTime) / 1000.0F;
    _selector_transition_mix.update(current_time);
    _edit_mix.update(current_time);

    const float edit_mix = _edit_mix.directValue();
    if (std::abs(edit_mix - _last_edit_mix) > 0.0005F) {
        _last_edit_mix    = edit_mix;
        _render_requested = true;
    }
    if (!editing() && _edit_mix.done() && std::abs(_last_edit_mix) <= 0.0005F) {
        _edit_visual_item_index = -1;
    }

    const float entry_mask_opacity = _entry_mask_opacity.value();
    if (std::abs(entry_mask_opacity - _last_entry_mask_opacity) > 0.001F) {
        _last_entry_mask_opacity = entry_mask_opacity;
        _render_requested        = true;
    }

    for (auto& item_layout : _item_layouts) {
        if (item_layout.item != nullptr && item_layout.item->update(currentTime)) {
            _render_requested = true;
        }
    }

    if (_page_transition_phase == PageTransitionPhase::Returning && getSelectorPostion().done() &&
        getSelectorShape().done() && _selector_transition_mix.done()) {
        _selector_foreground   = false;
        _page_transition_phase = PageTransitionPhase::Inactive;
        for (AnimateValue* value : {&getCamera().x, &getCamera().y}) {
            if (value->currentPlayingState() == AnimateState::Paused) {
                value->play();
            }
        }
        configureSelectorAnimations();
        _render_requested = true;
    }
}

void MenuView::onRender()
{
    if (_item_layouts.empty()) {
        return;
    }

    const auto camera            = getCamera().directValue();
    const auto selector_position = getSelectorPostion().directValue();
    const auto selector_shape    = getSelectorShape().directValue();
    const std::uint32_t now      = GetHAL().millis();

    float motion_target = 0.0F;
    if (_motion_sample_valid) {
        const std::uint32_t elapsed = now - _last_motion_tick;
        if (elapsed > 0U && !isPressing() && !editing() && _page_transition_phase == PageTransitionPhase::Inactive) {
            const float speed =
                std::abs(selector_position.y - _last_selector_y) * 1000.0F / static_cast<float>(elapsed);
            motion_target =
                std::clamp((speed - MotionSpeedDeadZone) / (MotionSpeedFullScale - MotionSpeedDeadZone), 0.0F, 1.0F);
        }
    }
    _last_selector_y     = selector_position.y;
    _last_motion_tick    = now;
    _motion_sample_valid = true;

    if (motion_target != _selector_motion.end) {
        _selector_motion = motion_target;
    }
    const float motion = _page_transition_phase == PageTransitionPhase::Inactive ? _selector_motion.value() : 0.0F;
    const float selector_width      = std::max(1.0F, selector_shape.x - MotionWidthCompression * motion);
    const float selector_height     = std::max(1.0F, selector_shape.y - MotionHeightCompression * motion);
    const float selector_centered_x = selector_position.x + (selector_shape.x - selector_width) * 0.5F;
    const float selector_centered_y = selector_position.y - (selector_height - selector_shape.y) * 0.5F;
    const float selector_opacity    = _page_transition_phase == PageTransitionPhase::Inactive
                                          ? motion * MotionColorFade
                                          : _selector_transition_mix.directValue();
    const std::uint32_t selector_start_color =
        _page_transition_phase == PageTransitionPhase::Opening ? _theme.surface : _theme.selector;
    const std::uint32_t selector_color =
        color::blend_in_opacity(color::Rgb_t(selector_start_color), color::Rgb_t(_theme.background), selector_opacity)
            .toHex();
    const RenderState next_state = {
        roundToInt(camera.x),
        roundToInt(camera.y),
        roundToInt(selector_centered_x),
        roundToInt(selector_centered_y),
        std::max(1, roundToInt(selector_width)),
        std::max(1, roundToInt(selector_height)),
        selector_color,
        _data.selected_option_index,
        _selector_foreground,
    };

    if (!_render_requested && _render_state_valid && next_state == _render_state) {
        return;
    }

    _render_state            = next_state;
    _render_state_valid      = true;
    _render_requested        = false;
    _embedded_render_pending = true;
    if (!_embedded) {
        draw(GetHAL().gfxCanvas());
        GetHAL().presentGfx();
    }
}

void MenuView::onRelease()
{
    configureReleaseAnimations();
}

void MenuView::onClick()
{
    configureSelectorAnimations();
    auto* item = selectedItem();
    if (item == nullptr || !item->enabled()) {
        return;
    }

    if (editing()) {
        leaveEdit(true);
        return;
    }

    if (item->activate() == ItemActivation::BeginEdit) {
        item->beginEdit();
        _interaction            = InteractionState::Edit;
        _edit_visual_item_index = _data.selected_option_index;
        _edit_mix               = 1.0F;
    }
    _render_requested = true;
}

void MenuView::onOpenEnd()
{
    if (_page_transition_phase != PageTransitionPhase::Opening) {
        return;
    }

    auto on_complete         = std::move(_transition_out_complete);
    _transition_out_complete = nullptr;
    if (on_complete) {
        on_complete();
    }
}

void MenuView::_update_camera_keyframe()
{
    if (_item_layouts.empty()) {
        return;
    }

    const auto& selected = getSelectedKeyframe();
    float camera_y       = getCamera().y.end;
    if (selected.y < camera_y + CameraTopMargin) {
        camera_y = selected.y - CameraTopMargin;
    } else if (selected.y + selected.height > camera_y + ScreenHeight - CameraBottomMargin) {
        camera_y = selected.y + selected.height - ScreenHeight + CameraBottomMargin;
    }
    getCamera().move(0.0F, std::max(0.0F, camera_y));
}

void MenuView::setEmbedded(bool embedded)
{
    _embedded = embedded;
}

bool MenuView::takeRenderRequest()
{
    return std::exchange(_embedded_render_pending, false);
}

void MenuView::draw(lgfx::LGFX_Sprite& canvas) const
{
    canvas.clear(_theme.background);
    if (_underlay_draw_callback) _underlay_draw_callback(canvas);

    const auto draw_selector = [&]() {
        const int selector_x = _render_state.selectorX - _render_state.cameraX;
        const int selector_y = _render_state.selectorY - _render_state.cameraY;
        drawRectangle(canvas, selector_x, selector_y, _render_state.selectorWidth, _render_state.selectorHeight,
                      _render_state.selectorColor, SelectorRadius);
    };

    for (const auto& section : _section_layouts) {
        const int section_y = section.y - _render_state.cameraY;
        if (!isVisible(SectionTitleX - _render_state.cameraX, section_y, ScreenWidth - SectionTitleX * 2,
                       SectionTitleHeight)) {
            continue;
        }

        canvas.setTextSize(1);
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.setTextColor(_theme.secondaryText);
        canvas.setBaseColor(_theme.background);
        canvas.setFont(&SectionFont);
        canvas.drawString(section.title.data(), SectionTitleX - _render_state.cameraX, section_y);
    }

    if (!_render_state.selectorForeground) {
        draw_selector();
    }

    for (std::size_t index = 0; index < _item_layouts.size(); ++index) {
        const auto* item = _item_layouts[index].item;
        if (item == nullptr) {
            continue;
        }

        auto frame = item->frame();
        frame.x -= _render_state.cameraX;
        frame.y -= _render_state.cameraY;
        if (!isVisible(frame.x, frame.y, frame.width, frame.height)) {
            continue;
        }

        const bool edit_visual             = static_cast<int>(index) == _edit_visual_item_index;
        const ItemVisualState visual_state = {
            frame,
            _theme,
            static_cast<int>(index) == _render_state.selectedIndex,
            editing() && edit_visual,
            edit_visual ? std::clamp(_last_edit_mix, 0.0F, 1.0F) : 0.0F,
        };
        item->draw(canvas, visual_state);
    }

    if (_render_state.selectorForeground) {
        draw_selector();
    }

    const auto entry_mask_opacity =
        static_cast<std::uint8_t>(std::lround(std::clamp(_last_entry_mask_opacity, 0.0F, 255.0F)));
    if (entry_mask_opacity == 255U) {
        canvas.fillScreen(_theme.background);
    } else if (entry_mask_opacity != 0U) {
        canvas.fillRectAlpha(0, 0, ScreenWidth, ScreenHeight, entry_mask_opacity, _theme.background);
    }

    if (_overlay_draw_callback) {
        _overlay_draw_callback(canvas);
    }
}

}  // namespace settings::view
