/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../ir_signal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace ir_copy {

inline constexpr std::uint32_t BackgroundColor   = 0xF2FFFB;
inline constexpr std::size_t WaveformSampleCount = 17;
using Waveform                                   = std::array<std::uint8_t, WaveformSampleCount>;

enum class StatusWindowKind : std::uint8_t {
    None,
    Copying,
    CaptureSuccess,
    CaptureTimeout,
    CaptureFailed,
};

enum class SendFeedbackKind : std::uint8_t {
    None,
    Empty,
    Waveform,
};

class IrCopyView final {
public:
    ~IrCopyView();

    void init();
    void destroy();
    void update(std::uint32_t currentTime);
    void setPressedSlot(int slot);
    void setSlotCaptured(std::size_t index, bool captured);
    void setSlotWaveform(std::size_t index, const Waveform& waveform);
    void showSendFeedback(std::size_t slot, SendFeedbackKind kind);
    void showOperation(std::size_t slot, StatusWindowKind kind);
    void closeOperation();
    StatusWindowKind operation() const
    {
        return _status;
    }
    bool operationClosing() const
    {
        return _status_closing;
    }
    void redraw();

    void moveSlot(int direction);

    std::size_t selectedSlot() const
    {
        return _slot_index;
    }

private:
    struct RenderState {
        int decorationOffsetX      = 0;
        int decorationOffsetY      = 0;
        int badgeOffsetX           = 0;
        int badgeOffsetY           = 0;
        int waveformOffsetX        = 0;
        int indicatorOffsetX       = 0;
        int pageTransitionOffsetX  = 0;
        int selectorX              = 0;
        int selectorY              = 0;
        int selectorWidth          = 0;
        int selectorHeight         = 0;
        int selectorRadius         = 0;
        int operationProgress      = 0;
        int operationX             = 0;
        int operationY             = 0;
        int operationWidth         = 0;
        int operationHeight        = 0;
        int operationRadius        = 0;
        int statusBorderWidth      = 0;
        std::uint32_t statusBorder = 0;
        std::uint32_t statusFill   = 0;
        std::uint32_t statusText   = 0;
        int helpTextAlpha          = 0;
        int statusTextAlpha        = 0;
        int emptyShakeOffset       = 0;
        int waveformHighlight      = 0;
        std::size_t selectedSlot   = 0;
        std::size_t previousSlot   = 0;
        std::size_t operationSlot  = 0;
        std::size_t feedbackSlot   = SlotCount;
        std::array<bool, SlotCount> captured{};
        std::array<Waveform, SlotCount> waveforms{};
        StatusWindowKind status   = StatusWindowKind::None;
        bool statusClosing        = false;
        SendFeedbackKind feedback = SendFeedbackKind::None;
        bool pageTransitionActive = false;

        bool operator==(const RenderState& other) const;
    };

    RenderState sampleRenderState();
    void refreshPressedStyles();
    void configureStatusAnimation();
    void clearSendFeedback();
    void draw(std::uint32_t currentTime);
    void drawPageBadge(lgfx::LGFX_Sprite& canvas, int slot, int offsetX, int offsetY) const;
    void drawWaveformPanel(lgfx::LGFX_Sprite& canvas, int slot, int offsetX, int offsetY) const;

    std::array<bool, SlotCount> _captured{};
    std::array<Waveform, SlotCount> _waveforms{};
    std::array<uitk::AnimateValue, SlotCount> _slot_press;
    uitk::AnimateValue _entry_progress;
    uitk::AnimateValue _help_entry_progress;
    uitk::AnimateValue _page_progress;
    uitk::AnimateValue _operation_progress;
    uitk::AnimateValue _feedback_progress;
    std::uint32_t _last_update      = 0;
    std::uint32_t _last_render      = 0;
    std::uint32_t _status_closes_at = 0;
    std::size_t _slot_index         = 0;
    std::size_t _previous_slot      = 0;
    int _page_direction             = 0;
    bool _page_transition_active    = false;
    std::size_t _operation_slot     = 0;
    int _pressed_slot               = -1;
    bool _status_closing            = false;
    bool _render_requested          = true;
    bool _render_state_valid        = false;
    StatusWindowKind _status        = StatusWindowKind::None;
    SendFeedbackKind _feedback      = SendFeedbackKind::None;
    std::size_t _feedback_slot      = SlotCount;
    RenderState _render_state;
};

}  // namespace ir_copy
