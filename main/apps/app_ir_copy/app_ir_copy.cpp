/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ir_copy.h"

#include <apps/common/audio/audio.h>
#include <algorithm>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view Tag               = "IR Copy";
constexpr std::uint32_t LongPressMs          = 450;
constexpr std::uint32_t NavigationHoldMs     = 400;
constexpr std::uint32_t NavigationRepeatMs   = 70;
constexpr std::uint32_t NavigationFastAfter  = 1200;
constexpr std::uint32_t NavigationFastRepeat = 35;

ir_copy::Waveform makeWaveform(const ir_copy::Signal& signal)
{
    ir_copy::Waveform waveform{};
    if (signal.pulses.empty()) {
        return waveform;
    }

    std::uint64_t total_duration = 0;
    for (const auto& pulse : signal.pulses) {
        total_duration += pulse.durationUs;
    }
    if (total_duration == 0) {
        return waveform;
    }

    std::size_t pulse_index = 0;
    std::uint64_t pulse_end = signal.pulses.front().durationUs;
    for (std::size_t index = 0; index < waveform.size(); ++index) {
        const std::uint64_t sample_time = (static_cast<std::uint64_t>(index) * total_duration) / waveform.size();
        while (pulse_index + 1 < signal.pulses.size() && sample_time >= pulse_end) {
            pulse_end += signal.pulses[++pulse_index].durationUs;
        }
        waveform[index] = signal.pulses[pulse_index].high ? 1U : 0U;
    }
    return waveform;
}

}  // namespace

AppIrCopy::AppIrCopy()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "IR Copy";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_ir_copy);
    app_info.userData = &_theme_color;
}

void AppIrCopy::onCreate()
{
    mclog::tagInfo(Tag, "on create");
    loadStoredSignals();
}

void AppIrCopy::onOpen()
{
    mclog::tagInfo(Tag, "on open");

    if (!GetHAL().setDisplayRenderer(DisplayRenderer::Gfx)) {
        mclog::tagError(Tag, "failed to activate GFX renderer");
        close();
        return;
    }

    if (!GetHAL().setIrPowerEnabled(true) || !GetHAL().beginIrRaw()) {
        mclog::tagError(Tag, "failed to initialize raw IR");
        GetHAL().setIrPowerEnabled(false);
        close();
        return;
    }

    _pending_capture_index = NoSlot;
    _pressed_slot          = NoSlot;
    _pressed_at            = 0;
    _long_press_consumed   = false;
    _navigation_direction  = 0;
    _navigation_started_at = 0;
    _last_navigation_at    = 0;
    _view                  = std::make_unique<ir_copy::IrCopyView>();
    _view->init();
    for (std::size_t index = 0; index < _signals.size(); ++index) {
        _view->setSlotCaptured(index, !_signals[index].empty());
        _view->setSlotWaveform(index, makeWaveform(_signals[index]));
    }
}

void AppIrCopy::onRunning()
{
    auto& hal               = GetHAL();
    const std::uint32_t now = hal.millis();
    hal.updateButtonStates();

    if (hal.btnPwr.wasPressed()) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        close();
        return;
    }

    updateCapture();
    if (_view != nullptr) {
        updateNavigation(now);
        updateButtonGestures(now);
        // Sending raw IR can block; sample the clock again after input handling
        // so feedback started after the send does not immediately expire.
        _view->update(hal.millis());
    }
}

void AppIrCopy::onClose()
{
    mclog::tagInfo(Tag, "on close");

    cancelCapture();
    _pending_capture_index = NoSlot;
    _pressed_slot          = NoSlot;
    _pressed_at            = 0;
    _long_press_consumed   = false;
    _navigation_direction  = 0;
    _navigation_started_at = 0;
    _last_navigation_at    = 0;
    _view.reset();

    GetHAL().endIrRaw();
    GetHAL().setIrPowerEnabled(false);
}

void AppIrCopy::handleSlotClicked(std::size_t index)
{
    if (_view == nullptr || index >= _signals.size()) {
        return;
    }

    if (_signals[index].empty()) {
        audio::play_ui_sfx(audio::UiSfx::Boundary);
        _view->showSendFeedback(index, ir_copy::SendFeedbackKind::Empty);
        return;
    }

    if (sendSignal(_signals[index])) {
        audio::play_ui_sfx(audio::UiSfx::Confirm);
        _view->showSendFeedback(index, ir_copy::SendFeedbackKind::Waveform);
    }
}

void AppIrCopy::handleSlotLongPressed(std::size_t index)
{
    if (_view == nullptr || index >= _signals.size()) {
        return;
    }

    if (startCapture(index)) {
        _view->showOperation(index, ir_copy::StatusWindowKind::Copying);
    } else {
        _view->showOperation(index, ir_copy::StatusWindowKind::CaptureFailed);
    }
}

void AppIrCopy::updateCapture()
{
    if (_pending_capture_index >= _signals.size() || _view == nullptr) {
        return;
    }

    IrRawSignal signal;
    const IrRawCaptureResult result = GetHAL().pollIrRawCapture(signal);
    if (result == IrRawCaptureResult::Pending) {
        return;
    }

    const std::size_t captured_index = _pending_capture_index;
    _pending_capture_index           = NoSlot;
    switch (result) {
        case IrRawCaptureResult::Success:
            _signals[captured_index] = std::move(signal);
            _view->setSlotCaptured(captured_index, true);
            _view->setSlotWaveform(captured_index, makeWaveform(_signals[captured_index]));
            if (persistSignal(captured_index)) {
                _view->showOperation(captured_index, ir_copy::StatusWindowKind::CaptureSuccess);
            } else {
                mclog::tagError(Tag, "failed to persist slot {}", captured_index + 1);
                _view->showOperation(captured_index, ir_copy::StatusWindowKind::CaptureFailed);
            }
            break;
        case IrRawCaptureResult::Timeout:
            _view->showOperation(captured_index, ir_copy::StatusWindowKind::CaptureTimeout);
            break;
        case IrRawCaptureResult::Failed:
            _view->showOperation(captured_index, ir_copy::StatusWindowKind::CaptureFailed);
            break;
        case IrRawCaptureResult::Pending:
            break;
    }
}

void AppIrCopy::updateNavigation(std::uint32_t currentTime)
{
    auto& hal                   = GetHAL();
    const bool previous_pressed = hal.btnB.isPressed();
    const bool next_pressed     = hal.btnC.isPressed();
    const int direction         = previous_pressed == next_pressed ? 0 : (previous_pressed ? -1 : 1);
    if (direction != _navigation_direction) {
        _navigation_direction  = direction;
        _navigation_started_at = currentTime;
        _last_navigation_at    = currentTime;
    }

    if (_view == nullptr || _pending_capture_index != NoSlot || _pressed_slot != NoSlot ||
        _view->operation() != ir_copy::StatusWindowKind::None) {
        return;
    }

    if (hal.btnB.wasPressed()) {
        _view->moveSlot(-1);
        _last_navigation_at = currentTime;
        return;
    }
    if (hal.btnC.wasPressed()) {
        _view->moveSlot(1);
        _last_navigation_at = currentTime;
        return;
    }
    if (_navigation_direction == 0 || currentTime - _navigation_started_at < NavigationHoldMs) {
        return;
    }
    const std::uint32_t interval =
        currentTime - _navigation_started_at >= NavigationFastAfter ? NavigationFastRepeat : NavigationRepeatMs;
    if (currentTime - _last_navigation_at >= interval) {
        _last_navigation_at = currentTime;
        _view->moveSlot(_navigation_direction);
    }
}

void AppIrCopy::updateButtonGestures(std::uint32_t currentTime)
{
    if (_view == nullptr || _pending_capture_index != NoSlot || _view->operation() != ir_copy::StatusWindowKind::None) {
        return;
    }

    auto& button = GetHAL().btnA;
    if (_pressed_slot == NoSlot) {
        if (!button.wasPressed()) return;
        _pressed_slot        = _view->selectedSlot();
        _pressed_at          = currentTime;
        _long_press_consumed = false;
        _view->setPressedSlot(static_cast<int>(_pressed_slot));
        return;
    }

    if (button.isPressed() && !_long_press_consumed && currentTime - _pressed_at >= LongPressMs) {
        const std::size_t slot = _pressed_slot;
        _long_press_consumed   = true;
        _pressed_slot          = NoSlot;
        _view->setPressedSlot(-1);
        handleSlotLongPressed(slot);
        return;
    }

    if (button.wasReleased()) {
        const std::size_t slot = _pressed_slot;
        const bool was_long    = _long_press_consumed;
        _pressed_slot          = NoSlot;
        _pressed_at            = 0;
        _long_press_consumed   = false;
        _view->setPressedSlot(-1);
        if (!was_long) {
            handleSlotClicked(slot);
        }
    }
}

bool AppIrCopy::startCapture(std::size_t index)
{
    if (index >= _signals.size() || _pending_capture_index != NoSlot) {
        return false;
    }

    if (!GetHAL().startIrRawCapture(ir_copy::CaptureTimeoutMs)) {
        return false;
    }
    _pending_capture_index = index;
    return true;
}

bool AppIrCopy::sendSignal(const ir_copy::Signal& signal)
{
    if (signal.empty()) {
        return false;
    }

    return GetHAL().sendIrRaw(signal);
}

void AppIrCopy::cancelCapture()
{
    if (_pending_capture_index == NoSlot) {
        return;
    }

    GetHAL().cancelIrRawCapture();
    _pending_capture_index = NoSlot;
}

void AppIrCopy::loadStoredSignals()
{
    for (std::size_t index = 0; index < _signals.size(); ++index) {
        IrRawSignal signal;
        if (GetHAL().loadIrRawSignal(index, signal)) {
            _signals[index] = std::move(signal);
        }
    }
}

bool AppIrCopy::persistSignal(std::size_t index)
{
    return index < _signals.size() && GetHAL().saveIrRawSignal(index, _signals[index]);
}
