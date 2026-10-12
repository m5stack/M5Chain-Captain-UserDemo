/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"

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

constexpr int IconSize       = 128;
constexpr int IconImageSize  = 100;
constexpr int IconImageInset = 14;
constexpr int IconRadius     = 32;
constexpr int IconX          = 56;
constexpr int IconY          = 46;
constexpr int IconStep       = 160;

constexpr int SelectorPadding = 12;
constexpr int SelectorRadius  = 42;

constexpr int PanelX        = -7;
constexpr int PanelVisibleY = 203;
constexpr int PanelHiddenY  = 240;
constexpr int PanelWidth    = 254;
constexpr int PanelHeight   = 45;
constexpr int PanelRadius   = 23;

constexpr int IndicatorY          = 213;
constexpr int IndicatorDotSize    = 8;
constexpr int IndicatorActiveSize = 12;
constexpr int IndicatorStep       = 16;

constexpr std::uint32_t BackgroundColor   = 0xCBDEEE;
constexpr std::uint32_t SelectorColor     = 0x899BB9;
constexpr std::uint32_t PanelColor        = 0x6C7FA2;
constexpr std::uint32_t IndicatorColor    = 0x7C8FAE;
constexpr std::uint32_t IndicatorDimColor = 0x9CAFC8;
constexpr std::uint32_t TileColor         = 0xF4F4F5;
constexpr std::uint32_t DefaultThemeColor = 0x63717D;

constexpr float SelectorPositionVisualDuration   = 0.4F;
constexpr float SelectorShapeVisualDuration      = 0.5F;
constexpr float SelectorBounce                   = 0.3F;
constexpr float CameraVisualDuration             = 0.6F;
constexpr float CameraBounce                     = 0.2F;
constexpr float PressVisualDuration              = 0.18F;
constexpr float PressBounce                      = 0.1F;
constexpr float PressRestDelta                   = 0.5F;
constexpr float PressRestSpeed                   = 5.0F;
constexpr float OpenVisualDuration               = 0.35F;
constexpr float OpenBounce                       = 0.15F;
constexpr float OpenRestDelta                    = 2.0F;
constexpr float OpenRestSpeed                    = 15.0F;
constexpr float ColorDuration                    = 0.35F;
constexpr float PanelShowDuration                = 0.4F;
constexpr float PanelHideDuration                = 0.2F;
constexpr float PanelInitialDelay                = 0.3F;
constexpr std::uint32_t PanelReturnDelayMs       = 400;
constexpr float LoopArrivalTolerance             = 12.0F;
constexpr std::uint8_t NeutralInputPollsRequired = 2;
constexpr float MotionEaseDuration               = 0.08F;
constexpr float MotionSpeedDeadZone              = 20.0F;
constexpr float MotionSpeedFullScale             = 300.0F;
constexpr float MotionWidthCompression           = 32.0F;
constexpr float MotionHeightCompression          = 24.0F;
constexpr float MotionColorFade                  = 0.58F;

const lgfx::LVGLFont NameFont{&font_noto_sans_semibold_20};

int last_selected_index = -1;

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

bool isVisible(int x, int y, int width, int height)
{
    return x < ScreenWidth && x + width > 0 && y < ScreenHeight && y + height > 0;
}

bool allButtonsReleased()
{
    auto& hal = GetHAL();
    return !hal.btnPwr.isPressed() && !hal.btnA.isPressed() && !hal.btnB.isPressed() && !hal.btnC.isPressed();
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

void drawImage(lgfx::LGFX_Sprite& canvas, const gfx_image_dsc_t* image, int x, int y)
{
    constexpr std::size_t ExpectedStride = IconImageSize * sizeof(lgfx::swap565_t);
    constexpr std::size_t ExpectedSize   = IconImageSize * ExpectedStride;
    if (image == nullptr || image->format != GFX_IMAGE_FORMAT_RGB565_SWAPPED || image->width != IconImageSize ||
        image->height != IconImageSize || image->stride != ExpectedStride || image->data_size != ExpectedSize ||
        !isVisible(x, y, IconImageSize, IconImageSize)) {
        return;
    }

    canvas.pushImage(x, y, IconImageSize, IconImageSize, reinterpret_cast<const lgfx::swap565_t*>(image->data));
}

int indicatorCenterX(int pageCount, int index)
{
    return ScreenWidth / 2 + index * IndicatorStep - (pageCount - 1) * IndicatorStep / 2;
}

void drawPageIndicator(lgfx::LGFX_Sprite& canvas, int pageCount, int selectedIndex)
{
    if (pageCount <= 0) {
        return;
    }

    for (int index = 0; index < pageCount; ++index) {
        const bool selected = index == selectedIndex;
        const int dot_size  = selected ? IndicatorActiveSize : IndicatorDotSize;
        const int center_x  = indicatorCenterX(pageCount, index);
        drawRectangle(canvas, center_x - dot_size / 2, IndicatorY - dot_size / 2, dot_size, dot_size,
                      selected ? IndicatorColor : IndicatorDimColor, dot_size / 2);
    }
}

void configureEasing(AnimateValue& animation, float duration)
{
    animation.stop();
    auto& options          = animation.easingOptions();
    options.duration       = duration;
    options.easingFunction = ease::ease_out_back;
    animation.delay        = 0.0F;
    animation.begin();
}

void configureSpring(AnimateValue& animation, float visualDuration, float bounce)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = visualDuration;
    options.bounce         = bounce;
    animation.delay        = 0.0F;
}

void configureSpring(AnimateVector2& animation, float visualDuration, float bounce)
{
    configureSpring(animation.x, visualDuration, bounce);
    configureSpring(animation.y, visualDuration, bounce);
}

void setSpringResponse(AnimateVector2& animation, float visualDuration, float bounce, float restDelta = 0.1F,
                       float restSpeed = 0.1F)
{
    for (auto* value : {&animation.x, &animation.y}) {
        auto& options          = value->springOptions();
        options.duration       = 0.0F;
        options.visualDuration = visualDuration;
        options.bounce         = bounce;
        options.restDelta      = restDelta;
        options.restSpeed      = restSpeed;
    }
}

}  // namespace

namespace view {

LauncherView::~LauncherView()
{
    destroy();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appProps)
{
    destroy();
    _app_props = std::move(appProps);
    if (_app_props.empty()) {
        return;
    }

    setConfig().moveInLoop = true;
    setCameraSize(ScreenWidth, ScreenHeight);

    for (std::size_t index = 0; index < _app_props.size(); ++index) {
        addOption({{static_cast<float>(IconX + static_cast<int>(index) * IconStep), static_cast<float>(IconY),
                    static_cast<float>(IconSize), static_cast<float>(IconSize)},
                   nullptr});
    }

    configureAnimations();
    _status_bar.create();
    _status_bar.update();

    _selector_color.duration       = ColorDuration;
    _selector_color.easingFunction = ease::ease_out_quad;
    _selector_color.begin();
    auto& motion_options          = _selector_motion.easingOptions();
    motion_options.duration       = MotionEaseDuration;
    motion_options.easingFunction = ease::linear;
    _selector_motion.teleport(0.0F);

    _panel_y.teleport(PanelHiddenY);
    _panel_y.delay = PanelInitialDelay;
    _panel_y.move(PanelVisibleY);
    _panel_phase = PanelPhase::Showing;

    if (last_selected_index >= 0) {
        _is_restoring               = true;
        _data.selected_option_index = std::clamp(last_selected_index, 0, static_cast<int>(_app_props.size()) - 1);
        _panel_selected_index       = _data.selected_option_index;

        const auto& selected = getSelectedKeyframe();
        const float camera_x = selected.x - IconX;
        getCamera().teleport(camera_x, 0.0F);
        getSelectorPostion().teleport(camera_x, 0.0F);
        getSelectorShape().teleport(ScreenWidth, ScreenHeight);

        _selector_color.teleport(selectedThemeColor());
        _selector_color.move(SelectorColor);
    } else {
        _is_restoring                = false;
        _panel_selected_index        = 0;
        const float initial_camera_x = -ScreenWidth / 2.0F;
        getCamera().teleport(initial_camera_x, 0.0F);
        getSelectorPostion().teleport(initial_camera_x + ScreenWidth / 2.0F, 0.0F);
        getSelectorShape().teleport(0.0F, 0.0F);
        _selector_color.teleport(BackgroundColor);
        _selector_color.move(SelectorColor);
    }

    _name_visible        = true;
    _selector_foreground = false;
    _selector_color.update();
    onRender();
}

void LauncherView::destroy()
{
    _status_bar.destroy();
    _app_props.clear();
    _render_state           = {};
    _render_state_valid     = false;
    _render_requested       = true;
    _name_visible           = true;
    _selector_foreground    = false;
    _held_edge_direction    = 0;
    _navigation_input_state = NavigationInputState::Ready;
    _resume_held_navigation = false;
    _neutral_input_polls    = 0;
    _last_motion_tick       = 0;
    _last_selector_x        = 0.0F;
    _motion_sample_valid    = false;
    _is_restoring           = false;

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

void LauncherView::configureAnimations()
{
    configureSpring(getSelectorPostion(), SelectorPositionVisualDuration, SelectorBounce);
    configureSpring(getSelectorShape(), SelectorShapeVisualDuration, SelectorBounce);
    configureSpring(getCamera(), CameraVisualDuration, CameraBounce);
    configureEasing(_panel_y, PanelShowDuration);
}

void LauncherView::beginPanelHide()
{
    if (_panel_selected_index == getSelectedOptionIndex()) {
        return;
    }

    _panel_selected_index             = getSelectedOptionIndex();
    _name_visible                     = false;
    _panel_y.delay                    = 0.0F;
    _panel_y.easingOptions().duration = PanelHideDuration;
    _panel_y.move(PanelHiddenY);
    _panel_phase = PanelPhase::Hiding;
}

void LauncherView::beginPanelReturn()
{
    _name_visible                     = true;
    _panel_y.delay                    = 0.0F;
    _panel_y.easingOptions().duration = PanelShowDuration;
    _panel_y.move(PanelVisibleY);
    _panel_phase = PanelPhase::Showing;
}

void LauncherView::updatePanel(std::uint32_t currentTime)
{
    _panel_y.update(static_cast<float>(currentTime) / 1000.0F);

    if (_panel_phase == PanelPhase::Showing && _panel_y.done()) {
        _panel_phase = PanelPhase::Visible;
    } else if (_panel_phase == PanelPhase::Hiding && _panel_y.done()) {
        _return_at   = currentTime + PanelReturnDelayMs;
        _panel_phase = PanelPhase::Waiting;
    } else if (_panel_phase == PanelPhase::Waiting && static_cast<std::int32_t>(currentTime - _return_at) >= 0) {
        beginPanelReturn();
    }
}

bool LauncherView::atNavigationEdge(int direction)
{
    if (_app_props.empty()) {
        return true;
    }
    return direction < 0 ? getSelectedOptionIndex() == 0
                         : getSelectedOptionIndex() == static_cast<int>(_app_props.size()) - 1;
}

std::uint32_t LauncherView::selectedThemeColor() const
{
    const auto& info = _app_props[_data.selected_option_index].info;
    if (info.userData == nullptr) {
        return DefaultThemeColor;
    }
    return *static_cast<const std::uint32_t*>(info.userData);
}

void LauncherView::redraw()
{
    _render_requested = true;
    onRender();
}

void LauncherView::handleInput(input::KeyEvent event)
{
    if (_held_edge_direction != 0 && navigationButtonsReleased()) {
        _held_edge_direction = 0;
    }
    if (isOpening() || blockNavigationInput(event)) {
        return;
    }

    switch (event) {
        case input::KeyEvent::Home:
            if (!isPressing()) {
                if (getSelectedOptionIndex() != 0) {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                }
                goHome();
            }
            break;
        case input::KeyEvent::Previous:
            if (!isPressing()) {
                if (_app_props.size() > 1) {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                }
                goLast();
            }
            break;
        case input::KeyEvent::Next:
            if (!isPressing()) {
                if (_app_props.size() > 1) {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                }
                goNext();
            }
            break;
        case input::KeyEvent::PreviousRepeat:
        case input::KeyEvent::NextRepeat:
            if (!isPressing() && _held_edge_direction == 0) {
                const int direction = event == input::KeyEvent::PreviousRepeat ? -1 : 1;
                if (atNavigationEdge(direction)) {
                    _held_edge_direction = direction;
                    audio::play_ui_sfx(audio::UiSfx::Boundary);
                } else if (direction < 0) {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                    goLast();
                } else {
                    audio::play_ui_sfx(audio::UiSfx::Navigate);
                    goNext();
                }
            }
            break;
        case input::KeyEvent::OkPressed: {
            audio::play_ui_sfx(audio::UiSfx::Confirm);
            setSpringResponse(getSelectorPostion(), PressVisualDuration, PressBounce, PressRestDelta, PressRestSpeed);
            setSpringResponse(getSelectorShape(), PressVisualDuration, PressBounce, PressRestDelta, PressRestSpeed);
            const auto& selected = getSelectedKeyframe();
            press({selected.x + selected.width / 8.0F, selected.y + selected.height / 8.0F,
                   selected.width * 3.0F / 4.0F, selected.height * 3.0F / 4.0F});
            break;
        }
        case input::KeyEvent::OkReleased:
            if (isPressing()) {
                release();
            }
            break;
        default:
            break;
    }
}

void LauncherView::onRender()
{
    if (_app_props.empty()) {
        return;
    }

    const auto camera            = getCamera().directValue();
    const auto selector_position = getSelectorPostion().directValue();
    const auto selector_shape    = getSelectorShape().directValue();
    const std::uint32_t now      = GetHAL().millis();

    const bool motion_sampling_enabled = !isPressing() && !isOpening() && !_selector_foreground && !_is_restoring;
    const bool motion_visible          = !isOpening() && !_selector_foreground && !_is_restoring;
    float motion_target                = 0.0F;
    if (_motion_sample_valid) {
        const std::uint32_t elapsed = now - _last_motion_tick;
        if (elapsed > 0U && motion_sampling_enabled) {
            const float speed =
                std::abs(selector_position.x - _last_selector_x) * 1000.0F / static_cast<float>(elapsed);
            motion_target =
                std::clamp((speed - MotionSpeedDeadZone) / (MotionSpeedFullScale - MotionSpeedDeadZone), 0.0F, 1.0F);
        }
    }
    _last_selector_x     = selector_position.x;
    _last_motion_tick    = now;
    _motion_sample_valid = true;

    if (motion_target != _selector_motion.end) {
        _selector_motion = motion_target;
    }
    const float sampled_motion = _selector_motion.value();
    const float motion         = motion_visible ? sampled_motion : 0.0F;
    const float selector_width =
        std::max(1.0F, selector_shape.x + SelectorPadding * 2.0F - MotionWidthCompression * motion);
    const float selector_height =
        std::max(1.0F, selector_shape.y + SelectorPadding * 2.0F - MotionHeightCompression * motion);
    const float selector_x = selector_position.x - camera.x - SelectorPadding +
                             (selector_shape.x + SelectorPadding * 2.0F - selector_width) * 0.5F;
    const float selector_y = selector_position.y - camera.y - SelectorPadding +
                             (selector_shape.y + SelectorPadding * 2.0F - selector_height) * 0.5F;
    const std::uint32_t selector_color =
        color::blend_in_opacity(color::Rgb_t(_selector_color.toHex()), color::Rgb_t(BackgroundColor),
                                motion * MotionColorFade)
            .toHex();
    const RenderState next_state = {
        roundToInt(IconX - camera.x),
        roundToInt(IconY - camera.y),
        roundToInt(selector_x),
        roundToInt(selector_y),
        std::max(1, roundToInt(selector_width)),
        std::max(1, roundToInt(selector_height)),
        roundToInt(_panel_y.directValue()),
        getSelectedOptionIndex(),
        selector_color,
        _name_visible,
        _selector_foreground,
    };

    if (!_render_requested && _render_state_valid && next_state == _render_state) {
        return;
    }

    _render_state       = next_state;
    _render_state_valid = true;
    _render_requested   = false;
    draw();
    GetHAL().presentGfx();
}

void LauncherView::onUpdate(std::uint32_t currentTime)
{
    if (_status_bar.update()) {
        _render_requested = true;
    }
    _selector_color.update();
    updatePanel(currentTime);

    if (_is_restoring && !_data.is_changed && getSelectorPostion().done() && getSelectorShape().done()) {
        _is_restoring = false;
    }
}

void LauncherView::onGoLast()
{
    _is_restoring = false;
    if (_app_props.size() > 1 && getSelectedOptionIndex() == static_cast<int>(_app_props.size()) - 1) {
        beginNavigationInputLock(true);
    }
    beginPanelHide();
}

void LauncherView::onGoNext()
{
    _is_restoring = false;
    if (_app_props.size() > 1 && getSelectedOptionIndex() == 0) {
        beginNavigationInputLock(true);
    }
    beginPanelHide();
}

void LauncherView::goHome()
{
    if (getSelectedOptionIndex() == 0) {
        return;
    }

    _is_restoring = false;
    moveTo(0);
    _update_camera_keyframe();
    beginNavigationInputLock();
    beginPanelHide();
}

bool LauncherView::blockNavigationInput(input::KeyEvent event)
{
    if (_navigation_input_state == NavigationInputState::Ready) {
        return false;
    }

    if (_navigation_input_state == NavigationInputState::Animating) {
        if (navigationAnimationArrived()) {
            const bool navigation_held =
                !navigationButtonsReleased() && GetHAL().btnB.isPressed() != GetHAL().btnC.isPressed();
            _navigation_input_state = _resume_held_navigation && navigation_held
                                          ? NavigationInputState::Ready
                                          : NavigationInputState::AwaitingNeutral;
            _resume_held_navigation = false;
            _neutral_input_polls    = 0;
        }
        return true;
    }

    if (event == input::KeyEvent::None && allButtonsReleased()) {
        ++_neutral_input_polls;
        if (_neutral_input_polls >= NeutralInputPollsRequired) {
            _navigation_input_state = NavigationInputState::Ready;
            _neutral_input_polls    = 0;
        }
    } else {
        _neutral_input_polls = 0;
    }
    return true;
}

bool LauncherView::navigationAnimationArrived()
{
    const float target_x = getSelectedKeyframe().x - IconX;
    return std::abs(getCamera().x.directValue() - target_x) <= LoopArrivalTolerance;
}

void LauncherView::beginNavigationInputLock(bool resumeHeldNavigation)
{
    _navigation_input_state = NavigationInputState::Animating;
    _resume_held_navigation = resumeHeldNavigation;
    _neutral_input_polls    = 0;
}

void LauncherView::onClick()
{
    getCamera().x.pause();
    getCamera().y.pause();

    setSpringResponse(getSelectorPostion(), OpenVisualDuration, OpenBounce, OpenRestDelta, OpenRestSpeed);
    setSpringResponse(getSelectorShape(), OpenVisualDuration, OpenBounce, OpenRestDelta, OpenRestSpeed);

    const auto camera = getCamera().directValue();
    open({camera.x, camera.y, static_cast<float>(ScreenWidth), static_cast<float>(ScreenHeight)});
    const auto theme_color = selectedThemeColor();
    _selector_color.teleport(SelectorColor);
    _selector_color.move(theme_color);
    _selector_foreground = true;
    onRender();
}

void LauncherView::onOpenEnd()
{
    _render_requested = true;
    onRender();
    last_selected_index = getSelectedOptionIndex();
    if (onAppClicked) {
        onAppClicked(_app_props[getSelectedOptionIndex()].appID);
    }
}

void LauncherView::_update_camera_keyframe()
{
    const auto& selected = getSelectedKeyframe();
    _data.camera_offset.move(selected.x - IconX, 0.0F);
}

void LauncherView::draw() const
{
    auto& canvas = GetHAL().gfxCanvas();
    canvas.clear(BackgroundColor);

    const auto draw_selector = [&]() {
        drawRectangle(canvas, _render_state.selectorX, _render_state.selectorY, _render_state.selectorWidth,
                      _render_state.selectorHeight, _render_state.selectorColor, SelectorRadius);
    };

    _status_bar.draw(canvas);
    drawPageIndicator(canvas, static_cast<int>(_app_props.size()), _render_state.indicatorIndex);

    if (!_render_state.selectorForeground) {
        draw_selector();
    }

    for (std::size_t index = 0; index < _app_props.size(); ++index) {
        const int card_x = _render_state.contentX + static_cast<int>(index) * IconStep;
        const int card_y = _render_state.contentY;
        if (!isVisible(card_x, card_y, IconSize, IconSize)) {
            continue;
        }

        drawRectangle(canvas, card_x, card_y, IconSize, IconSize, TileColor, IconRadius);
        drawImage(canvas, static_cast<const gfx_image_dsc_t*>(_app_props[index].info.icon), card_x + IconImageInset,
                  card_y + IconImageInset);
    }

    drawRectangle(canvas, PanelX, _render_state.panelY, PanelWidth, PanelHeight, PanelColor, PanelRadius);
    if (_render_state.nameVisible && _panel_selected_index >= 0 &&
        _panel_selected_index < static_cast<int>(_app_props.size())) {
        canvas.setTextSize(1);
        canvas.setTextDatum(lgfx::textdatum_t::top_center);
        canvas.setTextColor(0xFFFFFF);
        canvas.setBaseColor(PanelColor);
        canvas.setFont(&NameFont);
        canvas.drawString(_app_props[static_cast<std::size_t>(_panel_selected_index)].info.name.c_str(),
                          ScreenWidth / 2, _render_state.panelY + 11);
    }

    if (_render_state.selectorForeground) {
        draw_selector();
    }
}

}  // namespace view
