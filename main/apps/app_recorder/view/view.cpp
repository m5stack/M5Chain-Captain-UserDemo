/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/toast/toast.h>
#include <array>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <hal/hal.h>

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;

constexpr int SlotWidth       = 194;
constexpr int SlotHeight      = 86;
constexpr int SlotRadius      = 31;
constexpr int SlotBorderWidth = 4;
constexpr int WindowX         = 10;
constexpr int WindowY         = 10;
constexpr int WindowSize      = 220;
constexpr int WindowRadius    = 30;
constexpr int WindowBorder    = 4;

constexpr std::uint32_t DecorationBackColor  = 0xCFD2FF;
constexpr std::uint32_t DecorationFrontColor = 0xE3E5FF;
constexpr std::uint32_t SlotColor            = 0xF8F8FF;
constexpr std::uint32_t RecordedSlotColor    = 0xE3E5FF;
constexpr std::uint32_t OutlineColor         = 0x696DA6;
constexpr std::uint32_t TextColor            = 0x6B6FA2;
constexpr std::uint32_t RecordingSurface     = 0xFFE9ED;
constexpr std::uint32_t RecordingAccent      = 0xE45D70;
constexpr std::uint32_t PlayingSurface       = 0xE2FFF2;

constexpr std::uint32_t PlayingAccent = 0x4DE19C;
constexpr std::uint32_t PlayingText   = 0x2DA86F;

constexpr std::uint32_t RenderInterval = 16;
constexpr bool EnablePageFade          = false;

const lgfx::LVGLFont InstructionFont{&font_noto_sans_semibold_24, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont SlotFont{&font_noto_sans_semibold_32, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont StatusFont{&font_noto_sans_semibold_24, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont DetailFont{&font_noto_sans_semibold_14, lgfx::lvgl_font_blend_t::destination};

struct SlotFrame {
    int x;
    int y;
    int labelX;
    int labelY;
};

constexpr std::array<SlotFrame, recorder::SlotCount> SlotFrames = {{
    {173, 22, 211, 64},
    {-127, 132, 28, 175},
    {173, 132, 210, 175},
}};

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
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

float smoothRange(float value, float start, float end)
{
    const float t = std::clamp((value - start) / (end - start), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

std::uint32_t slotColor(int mix)
{
    return blendColor(SlotColor, RecordedSlotColor, mix / 255.0F);
}

}  // namespace

namespace recorder {

RecorderView::~RecorderView()
{
    destroy();
}

void RecorderView::init()
{
    destroy();
    auto& options              = _operation_progress.springOptions();
    options.duration           = 0.0F;
    options.visualDuration     = 0.36F;
    options.bounce             = 0.06F;
    options.restDelta          = 0.001F;
    options.restSpeed          = 0.01F;
    _operation_progress.delay  = 0;
    _operation_progress.repeat = 0;
    _operation_progress.teleport(0.0F);
    _operation_progress.pause();
    for (std::size_t i = 0; i < SlotCount; ++i) {
        auto& easing          = _slot_style[i].easingOptions();
        easing.duration       = 0.12F;
        easing.easingFunction = uitk::ease::ease_out_cubic;
        _slot_style[i].teleport(_recorded[i] ? 1.0F : 0.0F);
        auto& press          = _slot_press[i].easingOptions();
        press.duration       = 0.12F;
        press.easingFunction = uitk::ease::ease_out_cubic;
        _slot_press[i].teleport(0.0F);
    }
    if constexpr (EnablePageFade) {
        auto& fade          = _page_mask.easingOptions();
        fade.duration       = 0.48F;
        fade.easingFunction = uitk::ease::ease_out_quad;
        _page_mask.teleport(255.0F);
        _page_mask = 0.0F;
        _page_mask.play();
    } else {
        _page_mask.pause();
        _page_mask.teleport(0.0F);
    }
    auto& entry          = _entry_progress.easingOptions();
    entry.duration       = 0.64F;
    entry.easingFunction = uitk::ease::linear;
    _entry_progress.teleport(0.0F);
    _entry_progress = 1.0F;
    _entry_progress.play();
    _last_update        = GetHAL().millis();
    _last_render        = _last_update;
    _render_state       = sampleRenderState();
    _render_state_valid = true;
    draw(_last_update);
}

void RecorderView::destroy()
{
    ::view::getToastManager().clear();
    _operation_progress.pause();
    _entry_progress.pause();
    _page_mask.pause();
    _exiting = false;
    for (auto& style : _slot_style) style.pause();
    for (auto& press : _slot_press) press.pause();
    _operation            = OperationKind::None;
    _operation_slot       = 0;
    _pressed_slot         = -1;
    _operation_started_at = 0;
    _elapsed_seconds      = 0;
    _last_update = _last_render = 0;
    _waveform.fill(0);
    _smooth_waveform.fill(0);
    _operation_closing  = false;
    _render_requested   = true;
    _render_state_valid = false;
}

void RecorderView::update(std::uint32_t currentTime)
{
    const float time = currentTime / 1000.0F;
    const float dt   = std::min(currentTime - _last_update, std::uint32_t{100}) / 1000.0F;
    _last_update     = currentTime;
    _entry_progress.update(time);
    _page_mask.update(time);
    for (auto& style : _slot_style) style.update(time);
    for (auto& press : _slot_press) press.update(time);
    if (_operation != OperationKind::None) {
        _operation_progress.update(time);
        if (_operation_closing && _operation_progress.done()) {
            _operation         = OperationKind::None;
            _operation_closing = false;
            _operation_progress.pause();
        }
        if (!_operation_closing) _elapsed_seconds = (currentTime - _operation_started_at) / 1000;
        for (std::size_t i = 0; i < _waveform.size(); ++i) {
            const float target   = _operation_closing ? 0.0F : _waveform[i];
            const float duration = target > _smooth_waveform[i] ? 0.045F : 0.12F;
            _smooth_waveform[i] += (target - _smooth_waveform[i]) * (1.0F - std::exp(-dt / duration));
        }
    }
    auto& toast = ::view::getToastManager();
    _render_requested |= toast.update(currentTime) || toast.hasPending();
    if (currentTime - _last_render < RenderInterval) return;
    const auto next = sampleRenderState();
    if (!_render_requested && _render_state_valid && next == _render_state) return;
    _render_state       = next;
    _render_state_valid = true;
    _last_render        = currentTime;
    draw(currentTime);
}

void RecorderView::redraw()
{
    _render_requested = true;
}

void RecorderView::setPressedSlot(int slot)
{
    if (_pressed_slot == slot) {
        return;
    }
    _pressed_slot = slot;
    refreshSlotStyles();
}

void RecorderView::setSlotRecorded(std::size_t slot, bool recorded)
{
    if (slot >= _recorded.size() || _recorded[slot] == recorded) {
        return;
    }
    _recorded[slot] = recorded;
    refreshSlotStyles();
}

void RecorderView::setWaveform(const std::array<std::uint8_t, 9>& waveform)
{
    if (_waveform == waveform) {
        return;
    }
    _waveform = waveform;
}

void RecorderView::showOperation(std::size_t slot, OperationKind kind)
{
    if (slot >= SlotCount || kind == OperationKind::None) return;
    // Another slot cannot steal a card while it is still returning home.
    if (_operation != OperationKind::None && (slot != _operation_slot || kind != _operation)) return;
    if (_operation != OperationKind::None && !_operation_closing) return;
    if (_operation == OperationKind::None) {
        _operation_progress.teleport(0.0F);
        _smooth_waveform.fill(0);
        _operation_started_at = GetHAL().millis();
        _elapsed_seconds      = 0;
    }
    ::view::getToastManager().dismiss();
    _operation_slot    = slot;
    _operation         = kind;
    _operation_closing = false;
    _pressed_slot      = -1;
    refreshSlotStyles();
    _operation_progress = 1.0F;
    _operation_progress.play();
    _render_requested = true;
}

void RecorderView::closeOperation()
{
    if (_operation == OperationKind::None || _operation_closing) return;
    // Retarget the current spring rather than resetting geometry or restarting
    // independent content delays. A quick release reverses the same card.
    _operation_progress = 0.0F;
    _operation_progress.play();
    _operation_closing = true;
    _render_requested  = true;
}

void RecorderView::beginExit()
{
    if (_exiting) return;
    _exiting = true;
    ::view::getToastManager().dismiss();
    if constexpr (EnablePageFade) {
        _page_mask.easingOptions().duration = 0.18F;
        _page_mask                          = 255.0F;
        _page_mask.play();
    } else {
        _page_mask.pause();
        _page_mask.teleport(255.0F);
    }
    _render_requested = true;
}

bool RecorderView::exiting() const
{
    return _exiting;
}

bool RecorderView::exitFinished()
{
    if constexpr (EnablePageFade) {
        return _exiting && _page_mask.done() && _render_state.pageMask == 255;
    }
    return _exiting && _render_state.pageMask == 255;
}

OperationKind RecorderView::operation() const
{
    return _operation;
}

bool RecorderView::operationClosing() const
{
    return _operation_closing;
}

void RecorderView::refreshSlotStyles()
{
    for (std::size_t i = 0; i < SlotCount; ++i) {
        _slot_style[i] = _recorded[i] ? 1.0F : 0.0F;
        _slot_press[i] = _pressed_slot == static_cast<int>(i) ? 1.0F : 0.0F;
    }
}

bool RecorderView::RenderState::operator==(const RenderState& other) const
{
    return std::tie(x, y, width, height, radius, border, labelX, labelY, style, label, content, pageMask, slotMix,
                    slotOffset, panelOffset, bars, seconds, operation, slot) ==
           std::tie(other.x, other.y, other.width, other.height, other.radius, other.border, other.labelX, other.labelY,
                    other.style, other.label, other.content, other.pageMask, other.slotMix, other.slotOffset,
                    other.panelOffset, other.bars, other.seconds, other.operation, other.slot);
}

RecorderView::RenderState RecorderView::sampleRenderState()
{
    RenderState result;
    result.operation = _operation;
    result.slot      = _operation_slot;
    result.pageMask  = roundToInt(std::clamp(_page_mask.directValue(), 0.0F, 255.0F));
    // A shared timeline gives the panels and staggered buttons a stable order.
    const float entry    = _entry_progress.directValue();
    const auto remaining = [entry](float start, float end, bool bounce = false) {
        const float t = std::clamp((entry - start) / (end - start), 0.0F, 1.0F);
        // A small overshoot settles back into place without extending the stagger.
        const float u = 1.0F - t;
        return bounce ? u * u * (2.0F * u - 1.0F) : u * u * u;
    };
    const float front  = remaining(0.0F, 0.68F);
    const float back   = remaining(0.08F, 0.78F);
    result.panelOffset = {roundToInt(-190 * front), roundToInt(-155 * front), roundToInt(210 * back),
                          roundToInt(180 * back)};
    for (std::size_t i = 0; i < SlotCount; ++i) {
        const std::size_t order = i == 0 ? 1 : (i == 1 ? 0 : 2);
        result.slotMix[i]       = roundToInt(_slot_style[i].directValue() * 255);
        const int direction     = SlotFrames[i].x < 0 ? -1 : 1;
        result.slotOffset[i] =
            direction * roundToInt(_slot_press[i].directValue() * 6 +
                                   80 * remaining(0.18F + order * 0.08F, 0.82F + order * 0.09F, true));
    }
    if (_operation == OperationKind::None) return result;
    const auto& source = SlotFrames[_operation_slot];
    const float p      = std::clamp(_operation_progress.directValue(), 0.0F, 1.0F);
    const auto lerp    = [p](int a, int b) { return roundToInt(a + (b - a) * p); };
    result.x           = lerp(source.x + result.slotOffset[_operation_slot], WindowX);
    result.y           = lerp(source.y, WindowY);
    result.width       = lerp(SlotWidth, WindowSize);
    result.height      = lerp(SlotHeight, WindowSize);
    result.radius      = lerp(SlotRadius, WindowRadius);
    result.border      = lerp(SlotBorderWidth, WindowBorder);
    result.labelX      = lerp(source.labelX + result.slotOffset[_operation_slot], WindowX + WindowSize / 2);
    result.labelY      = lerp(source.labelY, WindowY + WindowSize / 2);
    result.style       = roundToInt(smoothRange(p, 0.0F, 0.85F) * 255);
    result.label       = roundToInt((1.0F - smoothRange(p, 0.0F, 0.35F)) * 255);
    result.content     = roundToInt(smoothRange(p, 0.60F, 1.0F) * 255);
    if (result.content) {
        result.seconds = _elapsed_seconds;
        for (std::size_t i = 0; i < result.bars.size(); ++i)
            result.bars[i] = 4 + roundToInt(_smooth_waveform[i] * 40 / 255);
    }
    return result;
}

void RecorderView::draw(std::uint32_t currentTime)
{
    auto& canvas = GetHAL().gfxCanvas();
    canvas.setClipRect(0, 0, canvas.width(), canvas.height());
    canvas.clear(BackgroundColor);

    const auto& panel = _render_state.panelOffset;
    canvas.fillSmoothRoundRect(38 + panel[2], 69 + panel[3], 268, 188, 56, DecorationBackColor);
    canvas.fillSmoothRoundRect(-86 + panel[0], -42 + panel[1], 268, 188, 56, DecorationFrontColor);

    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    canvas.setFont(&InstructionFont);
    canvas.setTextColor(TextColor);
    canvas.setBaseColor(DecorationFrontColor);
    canvas.drawString("Tap to play,", 17 + panel[0], 29 + panel[1]);
    canvas.drawString("Hold to", 17 + panel[0], 62 + panel[1]);
    canvas.drawString("Record!", 17 + panel[0], 95 + panel[1]);

    const auto draw_slot = [&](std::size_t slot) {
        const auto& frame        = SlotFrames[slot];
        const std::uint32_t fill = slotColor(_render_state.slotMix[slot]);
        drawSurface(canvas, frame.x + _render_state.slotOffset[slot], frame.y, SlotWidth, SlotHeight, SlotRadius,
                    SlotBorderWidth, fill, OutlineColor);
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        canvas.setFont(&SlotFont);
        canvas.setTextColor(OutlineColor);
        canvas.setBaseColor(fill);
        const char label[] = {static_cast<char>('1' + slot), '\0'};
        canvas.drawString(label, frame.labelX + _render_state.slotOffset[slot], frame.labelY);
    };

    for (std::size_t slot = 0; slot < SlotCount; ++slot) {
        if (_operation == OperationKind::None || slot != _operation_slot) {
            draw_slot(slot);
        }
    }

    if (_operation != OperationKind::None) {
        const auto& frame = _render_state;
        const int x = frame.x, y = frame.y, width = frame.width, height = frame.height;
        const int radius = frame.radius, border = frame.border;
        const float style_mix             = frame.style / 255.0F;
        const std::uint32_t source_color  = slotColor(frame.slotMix[_operation_slot]);
        const std::uint32_t target_color  = _operation == OperationKind::Recording ? RecordingSurface : PlayingSurface;
        const std::uint32_t target_accent = _operation == OperationKind::Recording ? RecordingAccent : PlayingAccent;
        const std::uint32_t fill_color    = blendColor(source_color, target_color, style_mix);
        const std::uint32_t border_color  = blendColor(OutlineColor, target_accent, style_mix);
        drawSurface(canvas, x, y, width, height, radius, border, fill_color, border_color);

        const float label_mix = frame.label / 255.0F;
        if (label_mix > 0.0F) {
            canvas.setTextDatum(lgfx::textdatum_t::middle_center);
            canvas.setFont(&SlotFont);
            canvas.setTextColor(blendColor(fill_color, OutlineColor, label_mix));
            canvas.setBaseColor(fill_color);
            const char label[] = {static_cast<char>('1' + _operation_slot), '\0'};
            canvas.drawString(label, frame.labelX, frame.labelY);
        }

        const float content_mix = frame.content / 255.0F;
        if (content_mix > 0.0F && width > border * 2 && height > border * 2) {
            canvas.setClipRect(x + border, y + border, width - border * 2, height - border * 2);
            const std::uint32_t content_color = blendColor(
                fill_color, _operation == OperationKind::Recording ? RecordingAccent : PlayingText, content_mix);
            canvas.setBaseColor(fill_color);
            canvas.setTextColor(content_color);
            canvas.setTextDatum(lgfx::textdatum_t::middle_center);
            canvas.setFont(&DetailFont);
            const std::uint32_t elapsed_seconds = frame.seconds;
            char slot_text[32];
            std::snprintf(slot_text, sizeof(slot_text), "SLOT %u - %02u:%02u",
                          static_cast<unsigned>(_operation_slot + 1), static_cast<unsigned>(elapsed_seconds / 60U),
                          static_cast<unsigned>(elapsed_seconds % 60U));
            canvas.drawString(slot_text, x + width / 2, y + 31);

            constexpr int BarCount  = 9;
            constexpr int BarWidth  = 7;
            constexpr int BarGap    = 5;
            constexpr int WaveWidth = BarCount * BarWidth + (BarCount - 1) * BarGap;
            const int wave_x        = x + (width - WaveWidth) / 2;
            const int wave_y        = y + height / 2 - 8;
            for (int bar = 0; bar < BarCount; ++bar) {
                const int bar_height = frame.bars[bar];
                canvas.fillSmoothRoundRect(wave_x + bar * (BarWidth + BarGap), wave_y - bar_height / 2, BarWidth,
                                           bar_height, BarWidth / 2, content_color);
            }

            canvas.setFont(&StatusFont);
            canvas.drawString(_operation == OperationKind::Recording ? "Recording" : "Playing", x + width / 2,
                              y + height - 61);
            if (_operation == OperationKind::Recording) {
                canvas.setFont(&DetailFont);
                canvas.drawString("Release to stop", x + width / 2, y + height - 27);
            }
            canvas.setClipRect(0, 0, canvas.width(), canvas.height());
        }
    }

    ::view::getToastManager().draw(canvas, BackgroundColor);
    if (_render_state.pageMask == 255) {
        canvas.fillScreen(BackgroundColor);
    } else if (_render_state.pageMask > 0) {
        canvas.fillRectAlpha(0, 0, ScreenWidth, ScreenHeight, _render_state.pageMask, BackgroundColor);
    }
    _render_requested = false;
    GetHAL().presentGfx();
}

}  // namespace recorder
