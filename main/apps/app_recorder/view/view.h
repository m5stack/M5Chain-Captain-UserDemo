/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace recorder {

inline constexpr std::size_t SlotCount         = 3;
inline constexpr std::uint32_t BackgroundColor = 0xFFFFFF;
inline constexpr std::uint32_t AppThemeColor   = BackgroundColor;

enum class OperationKind : std::uint8_t {
    None,
    Recording,
    Playing,
};

class RecorderView {
public:
    ~RecorderView();

    void init();
    void destroy();
    void update(std::uint32_t currentTime);
    void redraw();
    void setPressedSlot(int slot);
    void setSlotRecorded(std::size_t slot, bool recorded);
    void setWaveform(const std::array<std::uint8_t, 9>& waveform);
    void showOperation(std::size_t slot, OperationKind kind);
    void closeOperation();
    void beginExit();
    bool exiting() const;
    bool exitFinished();
    OperationKind operation() const;
    bool operationClosing() const;

private:
    // Entry transforms and the operation morph share one sampled layout, so
    // input during entry continues from the visible card without a jump.
    struct RenderState {
        int x = 0, y = 0, width = 0, height = 0, radius = 0, border = 0;
        int labelX = 0, labelY = 0;
        int style = 0, label = 255, content = 0;
        int pageMask = 255;
        std::array<int, SlotCount> slotMix{};
        std::array<int, SlotCount> slotOffset{};
        std::array<int, 4> panelOffset{};  // Front x/y, back x/y.
        std::array<int, 9> bars{};
        std::uint32_t seconds   = 0;
        OperationKind operation = OperationKind::None;
        std::size_t slot        = 0;
        bool operator==(const RenderState& other) const;
    };
    void refreshSlotStyles();
    RenderState sampleRenderState();
    void draw(std::uint32_t currentTime);

    std::array<bool, SlotCount> _recorded{};
    std::array<std::uint8_t, 9> _waveform{};
    std::array<float, 9> _smooth_waveform{};
    std::array<uitk::AnimateValue, SlotCount> _slot_style;
    std::array<uitk::AnimateValue, SlotCount> _slot_press;
    uitk::AnimateValue _operation_progress;
    uitk::AnimateValue _entry_progress;
    uitk::AnimateValue _page_mask;
    bool _exiting = false;
    RenderState _render_state;
    bool _render_state_valid            = false;
    std::uint32_t _last_update          = 0;
    std::uint32_t _elapsed_seconds      = 0;
    OperationKind _operation            = OperationKind::None;
    std::size_t _operation_slot         = 0;
    int _pressed_slot                   = -1;
    std::uint32_t _operation_started_at = 0;
    std::uint32_t _last_render          = 0;
    bool _operation_closing             = false;
    bool _render_requested              = true;
};

}  // namespace recorder
