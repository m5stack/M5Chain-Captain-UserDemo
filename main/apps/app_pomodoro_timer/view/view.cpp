/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/gfx/alpha_renderer.h>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>
#include <hal/hal.h>
#include <vector>

using namespace uitk;

namespace {

constexpr int ScreenSize   = 240;
constexpr int ScreenCenter = ScreenSize / 2;
constexpr int PanelCount   = 3;

constexpr float Pi                         = 3.14159265358979323846F;
constexpr float LayoutVisualDuration       = 0.50F;
constexpr float LayoutBounce               = 0.22F;
constexpr float EntryOffset                = 92.0F;
constexpr float EntryVisualDuration        = 0.52F;
constexpr float EntryBounce                = 0.28F;
constexpr float InstructionFadeDuration    = 0.35F;
constexpr float IdleTiltDuration           = 0.10F;
constexpr float InstructionTiltOffset      = 7.0F;
constexpr float IdleTiltDeadZone           = 0.04F;
constexpr float IdleTiltFullScale          = 0.82F;
constexpr float IdleBottomWidthGrowth      = 30.0F;
constexpr float IdleBottomHeightGrowth     = 26.0F;
constexpr float IdleSideWidthGrowth        = 38.0F;
constexpr float IdleSideHeightGrowth       = 14.0F;
constexpr std::uint32_t RenderInterval     = 16;
constexpr std::uint32_t BackgroundColor    = 0xFDFCE7;
constexpr std::uint32_t InstructionColor   = 0x5C5C5C;
constexpr std::uint32_t ActiveTextColor    = 0xFDFCE7;
constexpr std::uint32_t InactiveTextColor  = 0xFFFFFF;
constexpr float TimerTextPivotY            = 141.0F;
constexpr float FinishedIconCenterY        = 118.0F;
constexpr float FinishedIconShakeAmplitude = 11.0F;

struct PanelStyle {
    const char* shortLabel;
    const char* name;
    std::uint32_t color;
    float rotation;
    float smallLabelNormalOffset;
};

constexpr std::array<PanelStyle, PanelCount> PanelStyles = {{
    {"25", "Pomodoro", 0xED6B6B, 0.0F, -2.0F},
    {"5", "Short Break", 0x8C8AD5, 90.0F, 0.0F},
    {"15", "Long Break", 0x0D9E94, -90.0F, -2.0F},
}};

struct FloatPoint {
    float x = 0.0F;
    float y = 0.0F;
};

std::size_t panelIndex(view::PomodoroPanel panel)
{
    return static_cast<std::size_t>(panel) - 1;
}

float clampUnit(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

float smoothStep(float value)
{
    const float clamped = clampUnit(value);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

std::uint8_t opacity(float value)
{
    return static_cast<std::uint8_t>(std::lround(clampUnit(value) * 255.0F));
}

std::uint32_t blendColor(std::uint32_t foreground, std::uint32_t background, float backgroundMix)
{
    const float mix          = clampUnit(backgroundMix);
    const auto blend_channel = [mix](std::uint32_t front, std::uint32_t back) {
        return static_cast<std::uint32_t>(
            std::lround(static_cast<float>(front) * (1.0F - mix) + static_cast<float>(back) * mix));
    };
    return blend_channel(foreground >> 16U & 0xFFU, background >> 16U & 0xFFU) << 16U |
           blend_channel(foreground >> 8U & 0xFFU, background >> 8U & 0xFFU) << 8U |
           blend_channel(foreground & 0xFFU, background & 0xFFU);
}

FloatPoint rotateAroundScreenCenter(const FloatPoint& point, float angle)
{
    const float radians = angle * Pi / 180.0F;
    const float cosine  = std::cos(radians);
    const float sine    = std::sin(radians);
    const float x       = point.x - static_cast<float>(ScreenCenter);
    const float y       = point.y - static_cast<float>(ScreenCenter);
    return {
        static_cast<float>(ScreenCenter) + cosine * x - sine * y,
        static_cast<float>(ScreenCenter) + sine * x + cosine * y,
    };
}

FloatPoint offsetFromBaseline(const FloatPoint& point, float rotation, float normalOffset)
{
    const float radians = rotation * Pi / 180.0F;
    return {
        point.x - std::sin(radians) * normalOffset,
        point.y + std::cos(radians) * normalOffset,
    };
}

void drawRotatedText(lgfx::LGFX_Sprite& canvas, const char* text, const lv_font_t* font,
                     const FloatPoint& baselineCenter, float rotation, std::uint32_t color, std::uint8_t textOpacity)
{
    gfx::drawCenteredLvglText(canvas, text, font, baselineCenter.x, baselineCenter.y,
                              static_cast<int>(std::lround(rotation / 90.0F)), color, textOpacity);
}

void drawLabel(lgfx::LGFX_Sprite& canvas, int x, int y, int width, const char* text, const lv_font_t* font,
               std::uint32_t color, std::uint8_t labelOpacity)
{
    if (labelOpacity == 0U) {
        return;
    }
    const float baseline = static_cast<float>(y + lv_font_get_line_height(font) - font->base_line);
    gfx::drawCenteredLvglText(canvas, text, font, static_cast<float>(x) + static_cast<float>(width) * 0.5F, baseline, 0,
                              color, labelOpacity);
}

void drawRotatedImage(lgfx::LGFX_Sprite& canvas, const lv_image_dsc_t* image, const FloatPoint& center, float rotation,
                      std::uint32_t color, std::uint8_t imageOpacity)
{
    if (image == nullptr || imageOpacity == 0U || image->header.cf != LV_COLOR_FORMAT_A8 || image->data == nullptr) {
        return;
    }
    gfx::drawRotatedA8Mask(canvas, image->data, static_cast<int>(image->header.w), static_cast<int>(image->header.h),
                           static_cast<int>(image->header.stride),
                           {0, 0, static_cast<int>(image->header.w), static_cast<int>(image->header.h)}, center.x,
                           center.y, rotation, color, imageOpacity);
}

void configureFinishedIconShake(AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = 0.12F;
    options.bounce         = 0.18F;
}

void configureEntrySpring(AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = EntryVisualDuration;
    options.bounce         = EntryBounce;
    options.restDelta      = 0.1F;
    options.restSpeed      = 0.1F;
    animation.delay        = 0.0F;
}

}  // namespace

namespace view {

PomodoroTimerView::Layout PomodoroTimerView::idleLayout()
{
    return {{
        {79.0F, 210.0F, 82.0F, 82.0F, 16.0F, 120.0F, 237.0F, 0.0F},
        {-52.0F, 79.0F, 82.0F, 82.0F, 16.0F, 3.0F, 120.0F, 0.0F},
        {210.0F, 79.0F, 82.0F, 82.0F, 16.0F, 238.0F, 120.0F, 0.0F},
    }};
}

PomodoroTimerView::Layout PomodoroTimerView::layoutFor(PomodoroPanel panel)
{
    auto layout = idleLayout();
    switch (panel) {
        case PomodoroPanel::Pomodoro:
            layout[0] = {-8.5F, 57.5F, 257.0F, 257.0F, 68.5F, 120.0F, 237.0F, 1.0F};
            layout[1] = {-52.0F, 3.0F, 82.0F, 82.0F, 16.0F, 3.0F, 44.0F, 0.0F};
            layout[2] = {211.0F, 3.0F, 82.0F, 82.0F, 16.0F, 238.0F, 44.0F, 0.0F};
            break;
        case PomodoroPanel::ShortBreak:
            layout[0] = {155.0F, 210.0F, 82.0F, 82.0F, 16.0F, 196.0F, 237.0F, 0.0F};
            layout[1] = {-74.5F, -8.5F, 257.0F, 257.0F, 68.5F, 3.0F, 120.0F, 1.0F};
            break;
        case PomodoroPanel::LongBreak:
            layout[0] = {3.0F, 210.0F, 82.0F, 82.0F, 16.0F, 44.0F, 237.0F, 0.0F};
            layout[2] = {57.5F, -8.5F, 257.0F, 257.0F, 68.5F, 238.0F, 120.0F, 1.0F};
            break;
        case PomodoroPanel::None:
            break;
    }
    return layout;
}

PomodoroTimerView::PanelVisual PomodoroTimerView::applyIdleTilt(PanelVisual panel, std::size_t index, float tiltX,
                                                                float tiltY)
{
    float direction = 0.0F;
    switch (index) {
        case 0:
            direction = -tiltY;
            break;
        case 1:
            direction = tiltX;
            break;
        case 2:
            direction = -tiltX;
            break;
        default:
            return panel;
    }

    const float directional_tilt = clampUnit((direction - IdleTiltDeadZone) / (IdleTiltFullScale - IdleTiltDeadZone));
    const float idle_mix         = 1.0F - clampUnit(panel.activeMix);
    const float amount           = directional_tilt * idle_mix;

    switch (index) {
        case 0: {
            const float width_growth  = IdleBottomWidthGrowth * amount;
            const float height_growth = IdleBottomHeightGrowth * amount;
            panel.x -= width_growth * 0.5F;
            panel.y -= height_growth;
            panel.width += width_growth;
            panel.height += height_growth;
            panel.labelCenterY -= height_growth * 0.5F;
            break;
        }
        case 1: {
            const float width_growth  = IdleSideWidthGrowth * amount;
            const float height_growth = IdleSideHeightGrowth * amount;
            panel.y -= height_growth * 0.5F;
            panel.width += width_growth;
            panel.height += height_growth;
            panel.labelCenterX += width_growth * 0.5F;
            break;
        }
        case 2: {
            const float width_growth  = IdleSideWidthGrowth * amount;
            const float height_growth = IdleSideHeightGrowth * amount;
            panel.x -= width_growth;
            panel.y -= height_growth * 0.5F;
            panel.width += width_growth;
            panel.height += height_growth;
            panel.labelCenterX -= width_growth * 0.5F;
            break;
        }
        default:
            break;
    }
    return panel;
}

PomodoroTimerView::PanelVisual PomodoroTimerView::applyEntryOffset(PanelVisual panel, std::size_t index, float offset)
{
    const float visible_offset = offset * (1.0F - clampUnit(panel.activeMix));
    switch (index) {
        case 0:
            panel.y += visible_offset;
            panel.labelCenterY += visible_offset;
            break;
        case 1:
            panel.x -= visible_offset;
            panel.labelCenterX -= visible_offset;
            break;
        case 2:
            panel.x += visible_offset;
            panel.labelCenterX += visible_offset;
            break;
        default:
            break;
    }
    return panel;
}

void PomodoroTimerView::drawRectangle(lgfx::LGFX_Sprite& canvas, const PanelVisual& panel, std::uint32_t color)
{
    const float active_mix = clampUnit(panel.activeMix);
    const int x            = static_cast<int>(std::lround(panel.x));
    const int y            = static_cast<int>(std::lround(panel.y));
    const int width        = static_cast<int>(std::lround(panel.width));
    const int height       = static_cast<int>(std::lround(panel.height));
    const int radius = std::max(0, std::min({static_cast<int>(std::lround(panel.radius)), width / 2, height / 2}));
    const int border_width = static_cast<int>(std::lround(active_mix * 3.0F));
    if (width <= 0 || height <= 0 || x >= ScreenSize || y >= ScreenSize || x + width <= 0 || y + height <= 0) {
        return;
    }

    if (border_width > 0) {
        canvas.fillSmoothRoundRect(x, y, width, height, radius, blendColor(color, BackgroundColor, active_mix));
        if (width > border_width * 2 && height > border_width * 2) {
            canvas.fillSmoothRoundRect(x + border_width, y + border_width, width - border_width * 2,
                                       height - border_width * 2, std::max(0, radius - border_width), color);
        }
    } else {
        canvas.fillSmoothRoundRect(x, y, width, height, radius, color);
    }
}

void PomodoroTimerView::preparePanelAnimation(PanelAnimation& animation)
{
    for (AnimateValue* value : {&animation.x, &animation.y, &animation.width, &animation.height, &animation.radius,
                                &animation.labelCenterX, &animation.labelCenterY, &animation.activeMix}) {
        auto& options          = value->springOptions();
        options.duration       = 0.0F;
        options.visualDuration = LayoutVisualDuration;
        options.bounce         = LayoutBounce;
    }
}

void PomodoroTimerView::teleportPanel(PanelAnimation& animation, const PanelVisual& panel)
{
    animation.x.teleport(panel.x);
    animation.y.teleport(panel.y);
    animation.width.teleport(panel.width);
    animation.height.teleport(panel.height);
    animation.radius.teleport(panel.radius);
    animation.labelCenterX.teleport(panel.labelCenterX);
    animation.labelCenterY.teleport(panel.labelCenterY);
    animation.activeMix.teleport(panel.activeMix);
}

void PomodoroTimerView::movePanel(PanelAnimation& animation, const PanelVisual& panel)
{
    animation.x            = panel.x;
    animation.y            = panel.y;
    animation.width        = panel.width;
    animation.height       = panel.height;
    animation.radius       = panel.radius;
    animation.labelCenterX = panel.labelCenterX;
    animation.labelCenterY = panel.labelCenterY;
    animation.activeMix    = panel.activeMix;
}

PomodoroTimerView::PanelVisual PomodoroTimerView::samplePanel(PanelAnimation& animation)
{
    return {
        animation.x.value(),
        animation.y.value(),
        animation.width.value(),
        animation.height.value(),
        animation.radius.value(),
        animation.labelCenterX.value(),
        animation.labelCenterY.value(),
        animation.activeMix.value(),
    };
}

bool PomodoroTimerView::panelAnimationDone(PanelAnimation& animation)
{
    return animation.x.done() && animation.y.done() && animation.width.done() && animation.height.done() &&
           animation.radius.done() && animation.labelCenterX.done() && animation.labelCenterY.done() &&
           animation.activeMix.done();
}

PomodoroTimerView::~PomodoroTimerView()
{
    destroy();
}

void PomodoroTimerView::init()
{
    destroy();
    _rendered_layout                = idleLayout();
    _remaining_seconds              = {25U * 60U, 5U * 60U, 15U * 60U};
    _active_panel                   = PomodoroPanel::None;
    _last_layout_render             = 0;
    _layout_animating               = false;
    _rendered_idle_tilt_x           = 0.0F;
    _rendered_idle_tilt_y           = 0.0F;
    _rendered_entry_offset          = EntryOffset;
    _rendered_instruction_opacity   = 0.0F;
    _rendered_finished_icon_shake   = 0.0F;
    _idle_tilt_animating            = false;
    _has_finished_timer             = false;
    _instruction_transition_visible = true;
    _render_requested               = true;
    _timer_finished.fill(false);

    _finished_icon_shake.complete();
    _finished_icon_shake.sequence().clear();
    _finished_icon_shake.setSequence({0.0F, FinishedIconShakeAmplitude, -FinishedIconShakeAmplitude,
                                      FinishedIconShakeAmplitude, -FinishedIconShakeAmplitude, 0.0F});
    _finished_icon_shake.repeat = -1;
    _finished_icon_shake.onPlay(configureFinishedIconShake);
    _finished_icon_shake.onStep(
        [](AnimateValue& animation, std::vector<float>&, int) { configureFinishedIconShake(animation); });

    for (AnimateValue* tilt : {&_idle_tilt_x, &_idle_tilt_y}) {
        auto& options          = tilt->easingOptions();
        options.duration       = IdleTiltDuration;
        options.easingFunction = ease::linear;
        tilt->teleport(0.0F);
    }

    configureEntrySpring(_entry_offset);
    _entry_offset.teleport(EntryOffset);
    _entry_offset = 0.0F;

    auto& instruction_options          = _instruction_entry_opacity.easingOptions();
    instruction_options.duration       = InstructionFadeDuration;
    instruction_options.easingFunction = ease::ease_out_quad;
    _instruction_entry_opacity.delay   = 0.0F;
    _instruction_entry_opacity.teleport(0.0F);
    _instruction_entry_opacity = 1.0F;

    for (std::size_t index = 0; index < PanelCount; ++index) {
        preparePanelAnimation(_panel_animations[index]);
        teleportPanel(_panel_animations[index], _rendered_layout[index]);
        const unsigned int minutes = static_cast<unsigned int>(_remaining_seconds[index] / 60U);
        const unsigned int seconds = static_cast<unsigned int>(_remaining_seconds[index] % 60U);
        std::snprintf(_timer_text[index].data(), _timer_text[index].size(), "%02u:%02u", minutes, seconds);
    }

    renderIfRequested();
}

void PomodoroTimerView::destroy()
{
    _layout_animating               = false;
    _idle_tilt_animating            = false;
    _has_finished_timer             = false;
    _instruction_transition_visible = true;
    _timer_finished.fill(false);
    _entry_offset.pause();
    _instruction_entry_opacity.pause();
    _finished_icon_shake.pause();
    _rendered_entry_offset        = 0.0F;
    _rendered_instruction_opacity = 0.0F;
    _rendered_finished_icon_shake = 0.0F;
    _last_layout_render           = 0;
    _active_panel                 = PomodoroPanel::None;
    _render_requested             = true;
}

void PomodoroTimerView::update(std::uint32_t currentTime)
{
    const bool finished_icon_visible =
        _active_panel != PomodoroPanel::None && _timer_finished[panelIndex(_active_panel)];
    if (_layout_animating || _idle_tilt_animating || !_entry_offset.done() || !_instruction_entry_opacity.done() ||
        finished_icon_visible) {
        applyAnimations(currentTime);
    }
    renderIfRequested();
}

void PomodoroTimerView::setActivePanel(PomodoroPanel panel)
{
    if (_active_panel == panel) {
        return;
    }

    _instruction_transition_visible = _active_panel == PomodoroPanel::None || panel == PomodoroPanel::None;
    const Layout target             = layoutFor(panel);
    for (std::size_t index = 0; index < PanelCount; ++index) {
        movePanel(_panel_animations[index], target[index]);
    }
    _layout_animating = true;
    _active_panel     = panel;
    applyAnimations(GetHAL().millis(), true);
}

void PomodoroTimerView::setIdleTilt(float x, float y)
{
    bool changed = false;
    if (x != _idle_tilt_x.end) {
        _idle_tilt_x = x;
        changed      = true;
    }
    if (y != _idle_tilt_y.end) {
        _idle_tilt_y = y;
        changed      = true;
    }
    _idle_tilt_animating = _idle_tilt_animating || changed;
}

void PomodoroTimerView::setRemainingSeconds(PomodoroPanel panel, std::uint32_t seconds)
{
    if (panel == PomodoroPanel::None) {
        return;
    }
    const std::size_t index = panelIndex(panel);
    if (_remaining_seconds[index] == seconds) {
        return;
    }

    _remaining_seconds[index]    = seconds;
    const unsigned int minutes   = static_cast<unsigned int>(seconds / 60U);
    const unsigned int remainder = static_cast<unsigned int>(seconds % 60U);
    std::snprintf(_timer_text[index].data(), _timer_text[index].size(), "%02u:%02u", minutes, remainder);
    _render_requested = true;
}

void PomodoroTimerView::setTimerFinished(PomodoroPanel panel, bool finished)
{
    if (panel == PomodoroPanel::None) {
        return;
    }

    const std::size_t index = panelIndex(panel);
    if (_timer_finished[index] == finished) {
        return;
    }

    _timer_finished[index]        = finished;
    const bool had_finished_timer = _has_finished_timer;
    _has_finished_timer = std::any_of(_timer_finished.begin(), _timer_finished.end(), [](bool value) { return value; });

    if (_has_finished_timer) {
        if (!had_finished_timer) {
            _finished_icon_shake.complete();
        }
        _finished_icon_shake.play();
    } else {
        _finished_icon_shake.pause();
        _rendered_finished_icon_shake = 0.0F;
    }

    if (!had_finished_timer || !_has_finished_timer || finished) {
        _render_requested = true;
    }
}

void PomodoroTimerView::draw(lgfx::LGFX_Sprite& canvas)
{
    canvas.fillScreen(BackgroundColor);

    float maximum_active_mix = 0.0F;
    for (const PanelVisual& panel : _rendered_layout) {
        maximum_active_mix = std::max(maximum_active_mix, panel.activeMix);
    }
    const std::uint8_t instruction_opacity =
        _instruction_transition_visible
            ? opacity((1.0F - smoothStep(maximum_active_mix)) * _rendered_instruction_opacity)
            : 0U;
    const int instruction_x = static_cast<int>(std::lround(_rendered_idle_tilt_x * InstructionTiltOffset));
    const int instruction_y = static_cast<int>(std::lround(_rendered_idle_tilt_y * InstructionTiltOffset));
    drawLabel(canvas, 62 + instruction_x, 63 + instruction_y, 116, "Flip to", &font_noto_sans_semibold_32,
              InstructionColor, instruction_opacity);
    drawLabel(canvas, 62 + instruction_x, 106 + instruction_y, 116, "start!", &font_noto_sans_semibold_32,
              InstructionColor, instruction_opacity);

    std::array<std::size_t, PanelCount> draw_order = {0, 1, 2};
    std::sort(draw_order.begin(), draw_order.end(), [this](std::size_t left, std::size_t right) {
        return _rendered_layout[left].activeMix < _rendered_layout[right].activeMix;
    });

    for (std::size_t index : draw_order) {
        const PanelVisual panel =
            applyIdleTilt(applyEntryOffset(_rendered_layout[index], index, _rendered_entry_offset), index,
                          _rendered_idle_tilt_x, _rendered_idle_tilt_y);
        const PanelStyle& style = PanelStyles[index];
        drawRectangle(canvas, panel, style.color);

        const float active_mix = smoothStep((panel.activeMix - 0.12F) / 0.72F);
        const float small_mix  = 1.0F - smoothStep(panel.activeMix / 0.42F);
        const FloatPoint small_label_baseline =
            offsetFromBaseline({panel.labelCenterX, panel.labelCenterY}, style.rotation, style.smallLabelNormalOffset);
        drawRotatedText(canvas, style.shortLabel, &font_noto_sans_semibold_24, small_label_baseline, style.rotation,
                        InactiveTextColor, opacity(small_mix));

        const FloatPoint timer_baseline = rotateAroundScreenCenter({120.0F, TimerTextPivotY}, style.rotation);
        if (_timer_finished[index]) {
            const FloatPoint icon_center = rotateAroundScreenCenter({120.0F, FinishedIconCenterY}, style.rotation);
            drawRotatedImage(canvas, &image_status_hourglass_bottom, icon_center,
                             style.rotation + _rendered_finished_icon_shake, ActiveTextColor, opacity(active_mix));
        } else {
            drawRotatedText(canvas, _timer_text[index].data(), &font_noto_sans_semibold_64, timer_baseline,
                            style.rotation, ActiveTextColor, opacity(active_mix));
        }
        const FloatPoint name_baseline = rotateAroundScreenCenter({120.0F, 196.0F}, style.rotation);
        drawRotatedText(canvas, style.name, &font_noto_sans_semibold_24, name_baseline, style.rotation, ActiveTextColor,
                        opacity(active_mix));
    }
}

void PomodoroTimerView::renderIfRequested()
{
    if (!_render_requested) {
        return;
    }

    auto& canvas = GetHAL().gfxCanvas();
    draw(canvas);
    _render_requested = false;
    GetHAL().presentGfx();
}

void PomodoroTimerView::applyAnimations(std::uint32_t currentTime, bool force)
{
    if (!force && currentTime - _last_layout_render < RenderInterval) {
        return;
    }

    _layout_animating = false;
    for (std::size_t index = 0; index < PanelCount; ++index) {
        _rendered_layout[index] = samplePanel(_panel_animations[index]);
        _layout_animating       = _layout_animating || !panelAnimationDone(_panel_animations[index]);
    }
    _rendered_idle_tilt_x         = _idle_tilt_x.value();
    _rendered_idle_tilt_y         = _idle_tilt_y.value();
    _rendered_entry_offset        = _entry_offset.value();
    _rendered_instruction_opacity = _instruction_entry_opacity.value();
    _rendered_finished_icon_shake = _has_finished_timer ? _finished_icon_shake.value() : 0.0F;
    _idle_tilt_animating          = !_idle_tilt_x.done() || !_idle_tilt_y.done();
    _last_layout_render           = currentTime;
    _render_requested             = true;
}

}  // namespace view
