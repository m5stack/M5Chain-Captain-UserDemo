/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_recorder.h"

#include <apps/common/audio/audio.h>
#include <apps/common/toast/toast.h>
#include <array>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <string_view>

namespace {

constexpr std::string_view Tag                       = "Recorder";
constexpr std::uint32_t LongPressMs                  = 450;
constexpr std::uint32_t MinimumRecordingWindowTimeMs = 1000;
constexpr std::uint32_t MinimumPlaybackWindowTimeMs  = 700;
constexpr std::uint32_t MaximumRecordingMs           = RecorderMaxSeconds * 1000U;
static_assert(recorder::SlotCount == RecorderSlotCount);

}  // namespace

AppRecorder::AppRecorder()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "Recorder";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_recorder);
    app_info.userData = &_theme_color;
}

void AppRecorder::onCreate()
{
    mclog::tagInfo(Tag, "on create");
}

void AppRecorder::onOpen()
{
    mclog::tagInfo(Tag, "on open");
    auto& hal = GetHAL();
    if (!hal.setDisplayRenderer(DisplayRenderer::Gfx)) {
        mclog::tagError(Tag, "failed to activate GFX renderer");
        close();
        return;
    }
    if (!hal.beginRecorder()) {
        mclog::tagError(Tag, "failed to initialize recorder HAL");
        close();
        return;
    }

    _pressed_slot            = NoSlot;
    _recording_slot          = NoSlot;
    _playing_slot            = NoSlot;
    _pressed_at              = 0;
    _recording_started_at    = 0;
    _recording_closes_at     = 0;
    _playback_closes_at      = 0;
    _long_press_consumed     = false;
    _recording_close_pending = false;
    _playback_finished       = false;
    _view                    = std::make_unique<recorder::RecorderView>();
    _view->init();
    for (std::size_t slot = 0; slot < _recorded.size(); ++slot) {
        _recorded[slot] = hal.hasRecorderClip(slot);
        _view->setSlotRecorded(slot, _recorded[slot]);
    }
}

void AppRecorder::onRunning()
{
    auto& hal               = GetHAL();
    const std::uint32_t now = hal.millis();
    hal.updateButtonStates();

    if (_view != nullptr && _view->exiting()) {
        _view->update(now);
        if (_view->exitFinished()) close();
        return;
    }

    if (hal.btnPwr.wasPressed() && _recording_slot == NoSlot) {
        if (_view != nullptr && _view->operation() == recorder::OperationKind::Playing) {
            audio::play_ui_sfx(audio::UiSfx::Back);
            if (_playing_slot != NoSlot) {
                stopPlayback();
            } else {
                _view->closeOperation();
            }
            return;
        }
        audio::play_ui_sfx(audio::UiSfx::Back);
        if (_view != nullptr) {
            _view->beginExit();
            _view->update(now);
        } else {
            close();
        }
        return;
    }

    if (_recording_slot != NoSlot) {
        const auto result = hal.pollRecorderCapture();
        if (result == RecorderCaptureResult::Failed) {
            failRecording();
        } else if (result == RecorderCaptureResult::Complete || hal.btnPwr.wasPressed() ||
                   buttonForSlot(_recording_slot).wasReleased() || now - _recording_started_at >= MaximumRecordingMs) {
            finishRecording();
        } else if (_view != nullptr) {
            _view->setWaveform(hal.getRecorderWaveform());
        }
    } else if (_recording_close_pending) {
        if (static_cast<std::int32_t>(now - _recording_closes_at) >= 0) {
            _recording_close_pending = false;
            _recording_closes_at     = 0;
            if (_view != nullptr) {
                _view->closeOperation();
            }
        }
    } else if (_playing_slot != NoSlot) {
        if (!_playback_finished) {
            const RecorderPlaybackResult result = hal.pollRecorderPlayback();
            if (result == RecorderPlaybackResult::Finished) {
                _playback_finished = true;
            } else if (result == RecorderPlaybackResult::Failed) {
                mclog::tagError(Tag, "playback failed");
                stopPlayback();
            } else if (_view != nullptr) {
                _view->setWaveform(hal.getRecorderWaveform());
            }
        }
        if (_playing_slot != NoSlot && _playback_finished &&
            static_cast<std::int32_t>(now - _playback_closes_at) >= 0) {
            stopPlayback();
        }
    } else if (_view != nullptr && _view->operation() == recorder::OperationKind::None) {
        updateButtonGestures(now);
    }

    if (_view != nullptr) {
        _view->update(now);
    }
}

void AppRecorder::onClose()
{
    mclog::tagInfo(Tag, "on close");
    auto& hal = GetHAL();
    hal.cancelRecorderCapture();
    hal.stopRecorderPlayback();
    hal.endRecorder();
    _pressed_slot            = NoSlot;
    _recording_slot          = NoSlot;
    _playing_slot            = NoSlot;
    _pressed_at              = 0;
    _recording_started_at    = 0;
    _recording_closes_at     = 0;
    _playback_closes_at      = 0;
    _long_press_consumed     = false;
    _recording_close_pending = false;
    _playback_finished       = false;
    _view.reset();
}

void AppRecorder::updateButtonGestures(std::uint32_t currentTime)
{
    if (_view == nullptr) {
        return;
    }

    if (_pressed_slot == NoSlot) {
        for (std::size_t slot = 0; slot < recorder::SlotCount; ++slot) {
            if (buttonForSlot(slot).wasPressed()) {
                _pressed_slot        = slot;
                _pressed_at          = currentTime;
                _long_press_consumed = false;
                _view->setPressedSlot(static_cast<int>(slot));
                break;
            }
        }
        return;
    }

    Button& button = buttonForSlot(_pressed_slot);
    if (button.isPressed() && !_long_press_consumed && currentTime - _pressed_at >= LongPressMs) {
        _long_press_consumed = true;
        startRecording(_pressed_slot);
        return;
    }

    if (button.wasReleased()) {
        const std::size_t released_slot = _pressed_slot;
        _pressed_slot                   = NoSlot;
        _view->setPressedSlot(-1);
        if (!_long_press_consumed) {
            startPlayback(released_slot, currentTime);
        }
    }
}

void AppRecorder::startRecording(std::size_t slot)
{
    if (_view == nullptr || slot >= _recorded.size()) {
        return;
    }
    if (!GetHAL().startRecorderCapture(slot)) {
        ::view::pop_a_toast("Microphone unavailable", ::view::ToastType::Error);
        _view->setPressedSlot(-1);
        _view->redraw();
        return;
    }

    _recording_slot          = slot;
    _recording_started_at    = GetHAL().millis();
    _recording_closes_at     = _recording_started_at + MinimumRecordingWindowTimeMs;
    _recording_close_pending = false;
    _view->setWaveform({});
    _view->showOperation(slot, recorder::OperationKind::Recording);
}

void AppRecorder::finishRecording()
{
    if (_recording_slot == NoSlot) {
        return;
    }
    const std::size_t slot  = _recording_slot;
    const bool saved        = GetHAL().finishRecorderCapture();
    const std::uint32_t now = GetHAL().millis();
    _recorded[slot]         = GetHAL().hasRecorderClip(slot);
    _recording_slot         = NoSlot;
    _pressed_slot           = NoSlot;
    _pressed_at             = 0;
    _long_press_consumed    = false;
    _recording_started_at   = 0;
    if (_view != nullptr) {
        _view->setPressedSlot(-1);
        _view->setSlotRecorded(slot, _recorded[slot]);
        if (static_cast<std::int32_t>(now - _recording_closes_at) >= 0) {
            _recording_closes_at = 0;
            _view->closeOperation();
        } else {
            _recording_close_pending = true;
        }
    }
    if (!saved) {
        ::view::pop_a_toast("Recording was not saved", ::view::ToastType::Error);
    }
}

void AppRecorder::failRecording()
{
    if (_recording_slot == NoSlot) {
        return;
    }
    const std::size_t slot = _recording_slot;
    GetHAL().cancelRecorderCapture();
    const std::uint32_t now = GetHAL().millis();
    _recorded[slot]         = GetHAL().hasRecorderClip(slot);
    _recording_slot         = NoSlot;
    _pressed_slot           = NoSlot;
    _pressed_at             = 0;
    _long_press_consumed    = false;
    _recording_started_at   = 0;
    if (_view != nullptr) {
        _view->setPressedSlot(-1);
        _view->setSlotRecorded(slot, _recorded[slot]);
        if (static_cast<std::int32_t>(now - _recording_closes_at) >= 0) {
            _recording_closes_at = 0;
            _view->closeOperation();
        } else {
            _recording_close_pending = true;
        }
    }
    ::view::pop_a_toast("Recording failed", ::view::ToastType::Error);
}

void AppRecorder::startPlayback(std::size_t slot, std::uint32_t currentTime)
{
    if (_view == nullptr || slot >= _recorded.size()) {
        return;
    }
    if (!_recorded[slot]) {
        audio::play_ui_sfx(audio::UiSfx::Boundary);
        return;
    }
    if (!GetHAL().startRecorderPlayback(slot)) {
        mclog::tagError(Tag, "failed to start slot {} playback", slot + 1);
        return;
    }

    _playing_slot       = slot;
    _playback_finished  = false;
    _playback_closes_at = currentTime + MinimumPlaybackWindowTimeMs;
    _view->setWaveform({});
    _view->showOperation(slot, recorder::OperationKind::Playing);
}

void AppRecorder::stopPlayback()
{
    GetHAL().stopRecorderPlayback();
    _playing_slot      = NoSlot;
    _playback_finished = false;
    if (_view != nullptr) {
        _view->setWaveform({});
        _view->closeOperation();
    }
}

Button& AppRecorder::buttonForSlot(std::size_t slot)
{
    if (slot == 0) {
        return GetHAL().btnA;
    }
    if (slot == 1) {
        return GetHAL().btnB;
    }
    return GetHAL().btnC;
}
