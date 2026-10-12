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

using namespace uitk;

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;

constexpr int DecorationX      = -72;
constexpr int DecorationY      = -17;
constexpr int DecorationWidth  = 268;
constexpr int DecorationHeight = 265;
constexpr int DecorationRadius = 56;

constexpr int HelpX      = 60;
constexpr int HelpY      = 11;
constexpr int HelpWidth  = 231;
constexpr int HelpHeight = 75;
constexpr int HelpRadius = 27;

constexpr int BadgeX      = -36;
constexpr int BadgeY      = -18;
constexpr int BadgeWidth  = 78;
constexpr int BadgeHeight = 60;
constexpr int BadgeRadius = 20;

constexpr int CardX      = 14;
constexpr int CardY      = 100;
constexpr int CardWidth  = 212;
constexpr int CardHeight = 109;
constexpr int CardRadius = 10;
constexpr int CardBorder = 3;

constexpr int IndicatorY          = 226;
constexpr int IndicatorDotSize    = 4;
constexpr int IndicatorActiveSize = 6;
constexpr int IndicatorStep       = 12;

constexpr std::uint32_t DecorationColor = 0xDEF4EE;
constexpr std::uint32_t HelpBorderColor = 0x729C91;
constexpr std::uint32_t CardBorderColor = 0x9FBBB3;
constexpr std::uint32_t HintColor       = 0xADC7C0;
constexpr std::uint32_t WaveformColor   = 0x39BF9C;

constexpr std::uint32_t RenderInterval = 16;
constexpr std::uint32_t StatusDuration = 800;

constexpr int StatusWindowX      = 10;
constexpr int StatusWindowY      = 10;
constexpr int StatusWindowSize   = 220;
constexpr int StatusWindowRadius = 30;
constexpr int StatusBorderWidth  = 3;

constexpr float EntryDuration                = 0.68F;
constexpr float DecorationEntryStart         = 0.00F;
constexpr float DecorationEntryEnd           = 0.64F;
constexpr int DecorationEntryOffsetX         = -156;
constexpr int DecorationEntryOffsetY         = -132;
constexpr int EntryMargin                    = 10;
constexpr int WaveformEntryOffsetX           = ScreenWidth + EntryMargin;
constexpr int IndicatorEntryOffsetX          = ScreenWidth;
constexpr float WaveformLineRadius           = 1.0F;
constexpr float PageTransitionVisualDuration = 0.32F;
constexpr float PageTransitionBounce         = 0.08F;
constexpr float HelpEntryDelay               = 0.0F;
constexpr float HelpEntryVisualDuration      = 0.38F;
constexpr float HelpEntryBounce              = 0.18F;
constexpr float StatusVisualDuration         = 0.36F;
constexpr float StatusBounce                 = 0.06F;
constexpr float PressDuration                = 0.12F;
constexpr float FeedbackStepDuration         = 0.045F;
constexpr float EmptyShakeDuration           = FeedbackStepDuration * 5.0F;
constexpr float WaveformFlashDuration        = 0.18F;
constexpr int FeedbackShakeAmplitude         = 7;
// Keep the complete help button outside the viewport on the first entry frame.
// Recorder's 80px card offset is enough for its 194px cards, but this button
// starts at x=60 and is 231px wide, so its offset must follow the geometry.
constexpr int EntryOffset    = ScreenWidth - HelpX + EntryMargin;
constexpr int PressOffset    = 6;
constexpr int PressDirection = 1;

constexpr std::uint32_t WaveformHighlightColor = 0x00F2A0;

static_assert(HelpX + EntryOffset > ScreenWidth);

const lgfx::LVGLFont HelpFont{&font_noto_sans_semibold_20};
const lgfx::LVGLFont BadgeFont{&font_noto_sans_semibold_24};
const lgfx::LVGLFont EmptyFont{&font_noto_sans_semibold_24};

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

float remaining(float progress, float start, float end, bool bounce = false)
{
    const float t = std::clamp((progress - start) / (end - start), 0.0F, 1.0F);
    const float u = 1.0F - t;
    return bounce ? u * u * (2.0F * u - 1.0F) : u * u * u;
}

float smoothRange(float value, float start, float end)
{
    const float t = std::clamp((value - start) / (end - start), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

float sampleEmptyShake(float progress)
{
    constexpr std::array<float, 6> Keyframes = {0.0F,
                                                static_cast<float>(FeedbackShakeAmplitude),
                                                static_cast<float>(-FeedbackShakeAmplitude),
                                                static_cast<float>(FeedbackShakeAmplitude - 2),
                                                static_cast<float>(-FeedbackShakeAmplitude + 3),
                                                0.0F};
    const float position                     = std::clamp(progress, 0.0F, 1.0F) * (Keyframes.size() - 1);
    const auto index                         = std::min(static_cast<std::size_t>(position), Keyframes.size() - 2);
    const float mix                          = ease::ease_out_cubic(position - static_cast<float>(index));
    return Keyframes[index] + (Keyframes[index + 1] - Keyframes[index]) * mix;
}

std::uint32_t blendColor(std::uint32_t from, std::uint32_t to, float progress)
{
    progress                 = std::clamp(progress, 0.0F, 1.0F);
    const auto blend_channel = [progress](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint32_t>(
            std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * progress));
    };
    const std::uint32_t red   = blend_channel((from >> 16U) & 0xFFU, (to >> 16U) & 0xFFU);
    const std::uint32_t green = blend_channel((from >> 8U) & 0xFFU, (to >> 8U) & 0xFFU);
    const std::uint32_t blue  = blend_channel(from & 0xFFU, to & 0xFFU);
    return (red << 16U) | (green << 8U) | blue;
}

void drawSurface(lgfx::LGFX_Sprite& canvas, int x, int y, int width, int height, int radius, int borderWidth,
                 std::uint32_t fill, std::uint32_t border)
{
    if (width <= 0 || height <= 0 || x >= ScreenWidth || y >= ScreenHeight || x + width <= 0 || y + height <= 0) {
        return;
    }
    radius      = std::clamp(radius, 0, std::min(width, height) / 2);
    borderWidth = std::clamp(borderWidth, 0, std::min(width, height) / 2);
    canvas.fillSmoothRoundRect(x, y, width, height, radius, border);
    if (width > borderWidth * 2 && height > borderWidth * 2) {
        canvas.fillSmoothRoundRect(x + borderWidth, y + borderWidth, width - borderWidth * 2, height - borderWidth * 2,
                                   std::max(0, radius - borderWidth), fill);
    }
}

int indicatorCenterX(int index)
{
    return ScreenWidth / 2 + (index - static_cast<int>(ir_copy::SlotCount - 1) / 2) * IndicatorStep;
}

void drawPageIndicator(lgfx::LGFX_Sprite& canvas, int selectedIndex, int offsetX)
{
    for (int index = 0; index < static_cast<int>(ir_copy::SlotCount); ++index) {
        const bool selected = index == selectedIndex;
        const int size      = selected ? IndicatorActiveSize : IndicatorDotSize;
        canvas.fillSmoothRoundRect(indicatorCenterX(index) + offsetX - size / 2, IndicatorY - size / 2, size, size,
                                   size / 2, selected ? CardBorderColor : HintColor);
    }
}

struct StatusStyle {
    std::uint32_t background;
    std::uint32_t border;
    std::uint32_t textColor;
    const char* text;
};

StatusStyle statusStyle(ir_copy::StatusWindowKind kind)
{
    switch (kind) {
        case ir_copy::StatusWindowKind::Copying:
            return {0xFFFBDE, 0xFEE126, 0xB6A531, "Copying..."};
        case ir_copy::StatusWindowKind::CaptureSuccess:
            return {0xE2FFF2, 0x4DE19C, 0x2DA86F, "Done!"};
        case ir_copy::StatusWindowKind::CaptureTimeout:
            return {0xFFF1E8, 0xFF9B54, 0xB85C1E, "Timed out"};
        case ir_copy::StatusWindowKind::CaptureFailed:
            return {0xFFE8E8, 0xF26D6D, 0xA33A3A, "Copy failed"};
        case ir_copy::StatusWindowKind::None:
        default:
            return {ir_copy::BackgroundColor, ir_copy::BackgroundColor, HintColor, ""};
    }
}

}  // namespace

namespace ir_copy {

bool IrCopyView::RenderState::operator==(const RenderState& other) const
{
    return decorationOffsetX == other.decorationOffsetX && decorationOffsetY == other.decorationOffsetY &&
           badgeOffsetX == other.badgeOffsetX && badgeOffsetY == other.badgeOffsetY &&
           waveformOffsetX == other.waveformOffsetX && indicatorOffsetX == other.indicatorOffsetX &&
           pageTransitionOffsetX == other.pageTransitionOffsetX && selectorX == other.selectorX &&
           selectorY == other.selectorY && selectorWidth == other.selectorWidth &&
           selectorHeight == other.selectorHeight && selectorRadius == other.selectorRadius &&
           operationProgress == other.operationProgress && operationX == other.operationX &&
           operationY == other.operationY && operationWidth == other.operationWidth &&
           operationHeight == other.operationHeight && operationRadius == other.operationRadius &&
           statusBorderWidth == other.statusBorderWidth && statusBorder == other.statusBorder &&
           statusFill == other.statusFill && statusText == other.statusText && helpTextAlpha == other.helpTextAlpha &&
           statusTextAlpha == other.statusTextAlpha && emptyShakeOffset == other.emptyShakeOffset &&
           waveformHighlight == other.waveformHighlight && selectedSlot == other.selectedSlot &&
           previousSlot == other.previousSlot && operationSlot == other.operationSlot &&
           feedbackSlot == other.feedbackSlot && captured == other.captured && waveforms == other.waveforms &&
           status == other.status && statusClosing == other.statusClosing && feedback == other.feedback &&
           pageTransitionActive == other.pageTransitionActive;
}

IrCopyView::~IrCopyView()
{
    destroy();
}

void IrCopyView::init()
{
    destroy();
    configureStatusAnimation();

    auto& entry          = _entry_progress.easingOptions();
    entry.duration       = EntryDuration;
    entry.easingFunction = ease::linear;
    _entry_progress.teleport(0.0F);
    _entry_progress = 1.0F;
    _entry_progress.play();

    auto& help                  = _help_entry_progress.springOptions();
    help.duration               = 0.0F;
    help.visualDuration         = HelpEntryVisualDuration;
    help.bounce                 = HelpEntryBounce;
    help.restDelta              = 0.001F;
    help.restSpeed              = 0.01F;
    _help_entry_progress.delay  = HelpEntryDelay;
    _help_entry_progress.repeat = 0;
    _help_entry_progress.teleport(0.0F);
    _help_entry_progress = 1.0F;
    _help_entry_progress.play();

    auto& page            = _page_progress.springOptions();
    page.duration         = 0.0F;
    page.visualDuration   = PageTransitionVisualDuration;
    page.bounce           = PageTransitionBounce;
    page.restDelta        = 0.001F;
    page.restSpeed        = 0.01F;
    _page_progress.delay  = 0.0F;
    _page_progress.repeat = 0;
    _page_progress.teleport(1.0F);
    _page_progress.pause();

    for (auto& press : _slot_press) {
        auto& options          = press.easingOptions();
        options.duration       = PressDuration;
        options.easingFunction = ease::ease_out_cubic;
        press.teleport(0.0F);
    }

    _operation_progress.teleport(0.0F);
    _operation_progress.pause();
    _slot_index             = 0;
    _previous_slot          = 0;
    _page_direction         = 0;
    _page_transition_active = false;
    _operation_slot         = 0;
    _pressed_slot           = -1;
    _status                 = StatusWindowKind::None;
    _status_closing         = false;

    _last_update        = GetHAL().millis();
    _last_render        = _last_update;
    _render_state       = sampleRenderState();
    _render_state_valid = true;
    draw(_last_update);
}

void IrCopyView::destroy()
{
    _entry_progress.pause();
    _entry_progress.teleport(0.0F);
    _help_entry_progress.pause();
    _help_entry_progress.teleport(0.0F);
    for (auto& press : _slot_press) {
        press.pause();
        press.teleport(0.0F);
    }
    _operation_progress.pause();
    _operation_progress.teleport(0.0F);
    _page_progress.pause();
    _page_progress.teleport(1.0F);
    _feedback_progress.pause();
    _feedback_progress.teleport(0.0F);
    _captured.fill(false);
    for (auto& waveform : _waveforms) waveform.fill(0);
    _last_update            = 0;
    _last_render            = 0;
    _status_closes_at       = 0;
    _slot_index             = 0;
    _previous_slot          = 0;
    _page_direction         = 0;
    _page_transition_active = false;
    _operation_slot         = 0;
    _pressed_slot           = -1;
    _status_closing         = false;
    _render_requested       = true;
    _render_state_valid     = false;
    _status                 = StatusWindowKind::None;
    _feedback               = SendFeedbackKind::None;
    _feedback_slot          = SlotCount;
    _render_state           = {};
}

void IrCopyView::update(std::uint32_t currentTime)
{
    const float time = currentTime / 1000.0F;
    _last_update     = currentTime;

    _entry_progress.update(time);
    _help_entry_progress.update(time);
    _page_progress.update(time);
    if (_page_transition_active && _page_progress.done()) {
        _page_transition_active = false;
        _page_direction         = 0;
        _previous_slot          = _slot_index;
        _page_progress.pause();
        _render_requested = true;
    }
    for (auto& press : _slot_press) press.update(time);
    if (_feedback != SendFeedbackKind::None) _feedback_progress.update(time);
    if (_feedback != SendFeedbackKind::None && _feedback_progress.done()) {
        clearSendFeedback();
    }

    if (_status != StatusWindowKind::None) {
        _operation_progress.update(time);
        if (_status_closing && _operation_progress.done()) {
            _status           = StatusWindowKind::None;
            _status_closing   = false;
            _status_closes_at = 0;
            _operation_progress.pause();
            _render_requested = true;
        }
    }

    if (_status != StatusWindowKind::None && !_status_closing && _status != StatusWindowKind::Copying &&
        static_cast<std::int32_t>(currentTime - _status_closes_at) >= 0) {
        closeOperation();
    }

    if (currentTime - _last_render < RenderInterval) return;
    const auto next = sampleRenderState();
    if (!_render_requested && _render_state_valid && next == _render_state) return;
    _render_state       = next;
    _render_state_valid = true;
    _render_requested   = false;
    _last_render        = currentTime;
    draw(currentTime);
}

void IrCopyView::setSlotCaptured(std::size_t index, bool captured)
{
    if (index >= _captured.size() || _captured[index] == captured) return;
    _captured[index]  = captured;
    _render_requested = true;
}

void IrCopyView::setSlotWaveform(std::size_t index, const Waveform& waveform)
{
    if (index >= _waveforms.size() || _waveforms[index] == waveform) return;
    _waveforms[index] = waveform;
    _render_requested = true;
}

void IrCopyView::clearSendFeedback()
{
    _feedback      = SendFeedbackKind::None;
    _feedback_slot = SlotCount;
    _feedback_progress.pause();
    _feedback_progress.teleport(0.0F);
    _render_requested = true;
}

void IrCopyView::showSendFeedback(std::size_t slot, SendFeedbackKind kind)
{
    if (slot >= SlotCount || kind == SendFeedbackKind::None) return;

    clearSendFeedback();
    _feedback              = kind;
    _feedback_slot         = slot;
    auto& options          = _feedback_progress.easingOptions();
    options.duration       = kind == SendFeedbackKind::Empty ? EmptyShakeDuration : WaveformFlashDuration;
    options.easingFunction = ease::linear;
    _feedback_progress.cancel();
    _feedback_progress.teleport(0.0F);
    _feedback_progress = 1.0F;
    _feedback_progress.play();
    _render_requested = true;
}

void IrCopyView::setPressedSlot(int slot)
{
    if (_pressed_slot == slot) {
        return;
    }
    _pressed_slot = slot;
    refreshPressedStyles();
}

void IrCopyView::refreshPressedStyles()
{
    for (std::size_t i = 0; i < SlotCount; ++i) {
        _slot_press[i] = _pressed_slot == static_cast<int>(i) ? 1.0F : 0.0F;
    }
    _render_requested = true;
}

void IrCopyView::showOperation(std::size_t slot, StatusWindowKind kind)
{
    if (slot >= SlotCount || kind == StatusWindowKind::None) return;
    if (_status != StatusWindowKind::None && slot != _operation_slot) return;

    const bool was_hidden  = _status == StatusWindowKind::None;
    const bool was_closing = _status_closing;
    clearSendFeedback();
    if (was_hidden) {
        _operation_progress.teleport(0.0F);
    }
    _operation_slot   = slot;
    _status           = kind;
    _status_closing   = false;
    _status_closes_at = kind == StatusWindowKind::Copying ? 0 : GetHAL().millis() + StatusDuration;
    _pressed_slot     = -1;
    refreshPressedStyles();
    // Keep a Copying window in place while its result changes to Done/Failed.
    // Replaying a completed spring here makes the window jump back to the help
    // button and expand a second time. Only open a hidden window, or reverse a
    // close that was already in progress.
    if (was_hidden || was_closing) {
        _operation_progress = 1.0F;
        _operation_progress.play();
    }
    _render_requested = true;
}

void IrCopyView::closeOperation()
{
    if (_status == StatusWindowKind::None || _status_closing) {
        return;
    }
    _operation_progress = 0.0F;
    _operation_progress.play();
    _status_closing   = true;
    _render_requested = true;
}

void IrCopyView::redraw()
{
    _render_requested = true;
}

void IrCopyView::configureStatusAnimation()
{
    auto& options              = _operation_progress.springOptions();
    options.duration           = 0.0F;
    options.visualDuration     = StatusVisualDuration;
    options.bounce             = StatusBounce;
    options.restDelta          = 0.001F;
    options.restSpeed          = 0.01F;
    _operation_progress.delay  = 0.0F;
    _operation_progress.repeat = 0;
}

void IrCopyView::moveSlot(int direction)
{
    if (_status != StatusWindowKind::None || _pressed_slot >= 0 || direction == 0 || _page_transition_active) return;
    const int current = static_cast<int>(_slot_index);
    const int target  = (current + direction + static_cast<int>(SlotCount)) % static_cast<int>(SlotCount);
    audio::play_ui_sfx(audio::UiSfx::Navigate);
    clearSendFeedback();
    _previous_slot  = _slot_index;
    _slot_index     = static_cast<std::size_t>(target);
    _page_direction = direction < 0 ? -1 : 1;
    _page_progress.cancel();
    _page_progress.teleport(0.0F);
    _page_progress = 1.0F;
    _page_progress.play();
    _page_transition_active = true;
    _render_requested       = true;
}

IrCopyView::RenderState IrCopyView::sampleRenderState()
{
    RenderState result;
    result.selectedSlot         = _slot_index;
    result.previousSlot         = _previous_slot;
    result.operationSlot        = _operation_slot;
    result.captured             = _captured;
    result.waveforms            = _waveforms;
    result.status               = _status;
    result.statusClosing        = _status_closing;
    result.feedback             = _feedback;
    result.feedbackSlot         = _feedback_slot;
    result.pageTransitionActive = _page_transition_active;

    if (_feedback == SendFeedbackKind::Empty && _feedback_slot < SlotCount) {
        result.emptyShakeOffset = roundToInt(sampleEmptyShake(_feedback_progress.directValue()));
    } else if (_feedback == SendFeedbackKind::Waveform && _feedback_slot < SlotCount) {
        const float progress     = std::clamp(_feedback_progress.directValue(), 0.0F, 1.0F);
        const float peak         = 1.0F - std::abs(2.0F * progress - 1.0F);
        result.waveformHighlight = roundToInt(ease::ease_out_cubic(peak) * 255.0F);
    }

    const float entry = _entry_progress.directValue();
    result.decorationOffsetX =
        roundToInt(DecorationEntryOffsetX * remaining(entry, DecorationEntryStart, DecorationEntryEnd));
    result.decorationOffsetY =
        roundToInt(DecorationEntryOffsetY * remaining(entry, DecorationEntryStart, DecorationEntryEnd));
    result.badgeOffsetX       = result.decorationOffsetX;
    result.badgeOffsetY       = result.decorationOffsetY;
    const float helpRemaining = 1.0F - _help_entry_progress.directValue();
    result.waveformOffsetX    = roundToInt(WaveformEntryOffsetX * helpRemaining);
    result.indicatorOffsetX   = roundToInt(IndicatorEntryOffsetX * helpRemaining);

    const std::size_t pressSlot = _status == StatusWindowKind::None ? _slot_index : _operation_slot;
    const float press           = _slot_press[pressSlot].directValue();
    const float helpEntry       = 1.0F - _help_entry_progress.directValue();
    const int selectorOffset    = PressDirection * roundToInt(press * PressOffset + EntryOffset * helpEntry);
    result.selectorX            = HelpX + selectorOffset;
    result.selectorY            = HelpY;
    result.selectorWidth        = HelpWidth;
    result.selectorHeight       = HelpHeight;
    result.selectorRadius       = HelpRadius;

    if (_page_transition_active) {
        const float progress         = std::clamp(_page_progress.directValue(), 0.0F, 1.0F);
        result.pageTransitionOffsetX = _page_direction * roundToInt(ScreenWidth * (1.0F - progress));
    }

    if (_status == StatusWindowKind::None) return result;

    const float progress = std::clamp(_operation_progress.directValue(), 0.0F, 1.0F);
    const auto lerp      = [progress](int from, int to) {
        return roundToInt(static_cast<float>(from) + static_cast<float>(to - from) * progress);
    };
    const auto style         = statusStyle(_status);
    result.operationProgress = roundToInt(progress * 255.0F);
    result.operationX        = lerp(result.selectorX, StatusWindowX);
    result.operationY        = lerp(result.selectorY, StatusWindowY);
    result.operationWidth    = lerp(result.selectorWidth, StatusWindowSize);
    result.operationHeight   = lerp(result.selectorHeight, StatusWindowSize);
    result.operationRadius   = lerp(result.selectorRadius, StatusWindowRadius);
    result.statusBorderWidth = StatusBorderWidth;
    result.statusBorder      = blendColor(HelpBorderColor, style.border, progress);
    result.statusFill        = blendColor(BackgroundColor, style.background, progress);
    result.statusText        = style.textColor;
    result.helpTextAlpha     = roundToInt((1.0F - smoothRange(progress, 0.0F, 0.35F)) * 255.0F);
    result.statusTextAlpha   = roundToInt(smoothRange(progress, 0.55F, 1.0F) * 255.0F);
    return result;
}

void IrCopyView::drawPageBadge(lgfx::LGFX_Sprite& canvas, int slot, int offsetX, int offsetY) const
{
    if (slot < 0 || slot >= static_cast<int>(SlotCount)) return;
    const int badgeX = BadgeX + offsetX;
    const int badgeY = BadgeY + offsetY;
    canvas.fillSmoothRoundRect(badgeX, badgeY, BadgeWidth, BadgeHeight, BadgeRadius, BackgroundColor);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setTextColor(HintColor);
    canvas.setBaseColor(BackgroundColor);
    canvas.setFont(&BadgeFont);
    const char number[] = {static_cast<char>('1' + slot), '\0'};
    canvas.drawString(number, badgeX + 55, badgeY + 37);
}

void IrCopyView::drawWaveformPanel(lgfx::LGFX_Sprite& canvas, int slot, int offsetX, int offsetY) const
{
    if (slot < 0 || slot >= static_cast<int>(SlotCount)) return;
    const int cardX = CardX + offsetX;
    const int cardY = CardY + offsetY;
    drawSurface(canvas, cardX, cardY, CardWidth, CardHeight, CardRadius, CardBorder, BackgroundColor, CardBorderColor);
    const int clipX      = std::max(0, cardX + CardBorder);
    const int clipY      = std::max(0, cardY + CardBorder);
    const int clipRight  = std::min(ScreenWidth, cardX + CardWidth - CardBorder);
    const int clipBottom = std::min(ScreenHeight, cardY + CardHeight - CardBorder);
    if (clipRight <= clipX || clipBottom <= clipY) return;
    canvas.setClipRect(clipX, clipY, clipRight - clipX, clipBottom - clipY);
    if (!_render_state.captured[static_cast<std::size_t>(slot)]) {
        canvas.setFont(&EmptyFont);
        canvas.setTextColor(HintColor);
        canvas.setBaseColor(BackgroundColor);
        const int shake_offset = _render_state.feedback == SendFeedbackKind::Empty &&
                                         _render_state.feedbackSlot == static_cast<std::size_t>(slot)
                                     ? _render_state.emptyShakeOffset
                                     : 0;
        canvas.drawString("Empty", cardX + CardWidth / 2 + shake_offset, cardY + CardHeight / 2);
    } else {
        const auto& waveform               = _render_state.waveforms[static_cast<std::size_t>(slot)];
        const float highlight_mix          = _render_state.feedback == SendFeedbackKind::Waveform &&
                                            _render_state.feedbackSlot == static_cast<std::size_t>(slot)
                                                 ? _render_state.waveformHighlight / 255.0F
                                                 : 0.0F;
        const std::uint32_t waveform_color = blendColor(WaveformColor, WaveformHighlightColor, highlight_mix);
        constexpr int left                 = 35;
        constexpr int right                = 207;
        constexpr int highY                = 140;
        constexpr int lowY                 = 156;
        const auto drawWaveLine            = [&](int x0, int y0, int x1, int y1) {
            std::int32_t clipX, clipY, clipWidth, clipHeight;
            canvas.getClipRect(&clipX, &clipY, &clipWidth, &clipHeight);
            canvas.drawWideLine(x0, y0, x1, y1, WaveformLineRadius, waveform_color);
            canvas.setClipRect(clipX, clipY, clipWidth, clipHeight);
        };
        int previousX = left + offsetX;
        int previousY = (waveform.front() != 0 ? highY : lowY) + offsetY;
        for (std::size_t index = 1; index < waveform.size(); ++index) {
            const int x = left + offsetX + static_cast<int>((right - left) * index / (waveform.size() - 1));
            const int y = (waveform[index] != 0 ? highY : lowY) + offsetY;
            drawWaveLine(previousX, previousY, x, previousY);
            if (y != previousY) drawWaveLine(x, previousY, x, y);
            previousX = x;
            previousY = y;
        }
        drawWaveLine(previousX, previousY, right + offsetX, previousY);
    }
    canvas.setClipRect(0, 0, canvas.width(), canvas.height());
}

void IrCopyView::draw(std::uint32_t /*currentTime*/)
{
    auto& canvas = GetHAL().gfxCanvas();
    canvas.setClipRect(0, 0, canvas.width(), canvas.height());
    canvas.clear(BackgroundColor);
    canvas.fillSmoothRoundRect(DecorationX + _render_state.decorationOffsetX,
                               DecorationY + _render_state.decorationOffsetY, DecorationWidth, DecorationHeight,
                               DecorationRadius, DecorationColor);

    if (_render_state.status == StatusWindowKind::None) {
        drawSurface(canvas, _render_state.selectorX, _render_state.selectorY, _render_state.selectorWidth,
                    _render_state.selectorHeight, _render_state.selectorRadius, 3, BackgroundColor, HelpBorderColor);
        canvas.setTextDatum(lgfx::textdatum_t::middle_left);
        canvas.setFont(&HelpFont);
        canvas.setTextColor(HelpBorderColor);
        canvas.setBaseColor(BackgroundColor);
        canvas.drawString("Tap to send", _render_state.selectorX + 32, _render_state.selectorY + 23);
        canvas.drawString("Hold to copy!", _render_state.selectorX + 32, _render_state.selectorY + 50);
    }

    drawPageBadge(canvas, static_cast<int>(_render_state.selectedSlot), _render_state.badgeOffsetX,
                  _render_state.badgeOffsetY);
    const int waveformEntryOffsetX = _render_state.waveformOffsetX;
    if (_render_state.pageTransitionActive) {
        const int previousOffsetX =
            waveformEntryOffsetX + _render_state.pageTransitionOffsetX - _page_direction * ScreenWidth;
        const int selectedOffsetX = waveformEntryOffsetX + _render_state.pageTransitionOffsetX;
        drawWaveformPanel(canvas, static_cast<int>(_render_state.previousSlot), previousOffsetX, 0);
        drawWaveformPanel(canvas, static_cast<int>(_render_state.selectedSlot), selectedOffsetX, 0);
    } else {
        drawWaveformPanel(canvas, static_cast<int>(_render_state.selectedSlot), waveformEntryOffsetX, 0);
    }
    drawPageIndicator(canvas, static_cast<int>(_render_state.selectedSlot), _render_state.indicatorOffsetX);

    if (_render_state.status != StatusWindowKind::None) {
        const auto& frame = _render_state;
        const int x       = frame.operationX;
        const int y       = frame.operationY;
        const int width   = frame.operationWidth;
        const int height  = frame.operationHeight;
        const int radius  = std::clamp(frame.operationRadius, 0, std::min(width, height) / 2);
        if (frame.status == StatusWindowKind::Copying && frame.statusBorderWidth > 0) {
            const int connectorX     = x + width;
            const int connectorWidth = ScreenWidth - connectorX;
            if (connectorWidth > 0) {
                const int connectorY = y + (height - frame.statusBorderWidth) / 2;
                canvas.fillRect(connectorX, connectorY, connectorWidth, frame.statusBorderWidth, frame.statusBorder);
            }
        }
        drawSurface(canvas, x, y, width, height, radius, frame.statusBorderWidth, frame.statusFill, frame.statusBorder);

        const float progress = frame.operationProgress / 255.0F;
        const auto lerp      = [progress](int from, int to) {
            return roundToInt(static_cast<float>(from) + static_cast<float>(to - from) * progress);
        };
        const int targetTextX  = StatusWindowX + StatusWindowSize / 2 - 52;
        const int targetCenter = StatusWindowY + StatusWindowSize / 2;
        if (frame.helpTextAlpha > 0) {
            const std::uint32_t helpColor = blendColor(frame.statusFill, HelpBorderColor, frame.helpTextAlpha / 255.0F);
            canvas.setTextDatum(lgfx::textdatum_t::middle_left);
            canvas.setFont(&HelpFont);
            canvas.setTextColor(helpColor);
            canvas.setBaseColor(frame.statusFill);
            canvas.drawString("Tap to send", lerp(frame.selectorX + 32, targetTextX),
                              lerp(frame.selectorY + 23, targetCenter - 14));
            canvas.drawString("Hold to copy!", lerp(frame.selectorX + 32, targetTextX),
                              lerp(frame.selectorY + 50, targetCenter + 14));
        }

        if (frame.statusTextAlpha > 0) {
            const auto style = statusStyle(frame.status);
            const std::uint32_t statusColor =
                blendColor(frame.statusFill, frame.statusText, frame.statusTextAlpha / 255.0F);
            canvas.setTextDatum(lgfx::textdatum_t::middle_center);
            canvas.setFont(&HelpFont);
            canvas.setTextColor(statusColor);
            canvas.setBaseColor(frame.statusFill);
            canvas.drawString(style.text, x + width / 2, y + height / 2);
        }
    }

    _render_requested = false;
    GetHAL().presentGfx();
}

}  // namespace ir_copy
