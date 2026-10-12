/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <SDL2/SDL.h>
#include <apps/common/audio/audio.h>
#include <algorithm>
#include <array>
#include <mooncake_log.h>
#include <vector>

namespace {
constexpr auto Tag = "HAL-RECORDER";
enum class Mode { Idle, Capturing, Playing };
struct RecorderState {
    SDL_AudioDeviceID device = 0;
    std::array<std::vector<std::int16_t>, RecorderSlotCount> clips;
    std::vector<std::int16_t> pending;
    RecorderWaveform waveform{};
    std::size_t slot        = 0;
    std::size_t sampleLimit = 0;
    Mode mode               = Mode::Idle;
    bool initialized        = false;
};
// Explicitly released by recorderShutdown before the audio backend shuts down.
// Avoid cross-translation-unit static destruction ordering.
RecorderState& recorder = *new RecorderState;

void clearPending()
{
    std::vector<std::int16_t>{}.swap(recorder.pending);
    recorder.waveform.fill(0);
}

void updateWaveform(const std::int16_t* samples, std::size_t count)
{
    for (std::size_t point = 0; point < recorder.waveform.size(); ++point) {
        const auto begin = point * count / recorder.waveform.size();
        const auto end   = std::min(count, std::max(begin + 1, (point + 1) * count / recorder.waveform.size()));
        int peak         = 0;
        for (auto i = begin; i < end; ++i) peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
        recorder.waveform[point] = std::min(peak, 8192) * 255 / 8192;
    }
}

bool pumpCapturedAudio()
{
    if (recorder.mode != Mode::Capturing || !recorder.device) return false;
    if (SDL_GetAudioDeviceStatus(recorder.device) == SDL_AUDIO_STOPPED) return false;
    std::array<std::int16_t, 2048> buffer{};
    // Bound each poll by the queue snapshot and the 60-second memory limit.
    std::size_t remaining = SDL_GetQueuedAudioSize(recorder.device) / sizeof(std::int16_t);
    while (remaining && recorder.pending.size() < recorder.sampleLimit) {
        const auto count = std::min({remaining, buffer.size(), recorder.sampleLimit - recorder.pending.size()});
        const auto bytes = SDL_DequeueAudio(recorder.device, buffer.data(), count * sizeof(std::int16_t));
        if (!bytes || bytes % sizeof(std::int16_t)) return false;
        const auto read = bytes / sizeof(std::int16_t);
        recorder.pending.insert(recorder.pending.end(), buffer.begin(), buffer.begin() + read);
        updateWaveform(buffer.data(), read);
        remaining -= read;
    }
    if (recorder.pending.size() == recorder.sampleLimit) {
        SDL_PauseAudioDevice(recorder.device, 1);
        SDL_ClearQueuedAudio(recorder.device);
    }
    return true;
}
void endSession()
{
    if (recorder.device) {
        SDL_PauseAudioDevice(recorder.device, 1);
        SDL_ClearQueuedAudio(recorder.device);
        SDL_CloseAudioDevice(recorder.device);
    }
    if (recorder.mode == Mode::Playing) audio::stop_pcm();
    clearPending();
    recorder.device      = 0;
    recorder.mode        = Mode::Idle;
    recorder.initialized = false;
}

}  // namespace

bool Hal::beginRecorder()
{
    recorder.initialized = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0;
    return recorder.initialized;
}

void Hal::endRecorder()
{
    endSession();
    // Clips intentionally survive closing the app, but never touch disk.
}

bool Hal::hasRecorderClip(std::size_t slot) const
{
    return slot < recorder.clips.size() && !recorder.clips[slot].empty();
}

bool Hal::startRecorderCapture(std::size_t slot)
{
    if (!recorder.initialized || slot >= recorder.clips.size() || recorder.mode == Mode::Capturing) return false;
    stopRecorderPlayback();
    if (!recorder.device) {
        SDL_AudioSpec desired{};
        desired.freq     = static_cast<int>(_audio_sample_rate);
        desired.format   = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples  = 256;
        recorder.device  = SDL_OpenAudioDevice(nullptr, 1, &desired, nullptr, 0);
        if (!recorder.device) {
            mclog::tagError(Tag, "capture unavailable: {}", SDL_GetError());
            return false;
        }
    }
    clearPending();
    recorder.slot        = slot;
    recorder.sampleLimit = static_cast<std::size_t>(_audio_sample_rate) * RecorderMaxSeconds;
    recorder.pending.reserve(_audio_sample_rate);
    SDL_ClearQueuedAudio(recorder.device);
    recorder.mode = Mode::Capturing;
    SDL_PauseAudioDevice(recorder.device, 0);
    return true;
}

RecorderCaptureResult Hal::pollRecorderCapture()
{
    if (!pumpCapturedAudio()) return RecorderCaptureResult::Failed;
    return recorder.pending.size() == recorder.sampleLimit ? RecorderCaptureResult::Complete
                                                           : RecorderCaptureResult::Recording;
}

bool Hal::finishRecorderCapture()
{
    if (recorder.mode != Mode::Capturing) return false;
    SDL_PauseAudioDevice(recorder.device, 1);
    const bool captured = pumpCapturedAudio() && !recorder.pending.empty();
    if (captured) recorder.clips[recorder.slot].swap(recorder.pending);
    cancelRecorderCapture();
    return captured;
}

void Hal::cancelRecorderCapture()
{
    if (recorder.mode != Mode::Capturing) return;
    SDL_PauseAudioDevice(recorder.device, 1);
    SDL_ClearQueuedAudio(recorder.device);
    recorder.mode = Mode::Idle;
    clearPending();
}

bool Hal::startRecorderPlayback(std::size_t slot)
{
    if (!recorder.initialized || recorder.mode == Mode::Capturing || !hasRecorderClip(slot)) return false;
    audio::play_pcm(recorder.clips[slot]);
    recorder.mode = Mode::Playing;
    return true;
}

RecorderPlaybackResult Hal::pollRecorderPlayback()
{
    if (recorder.mode != Mode::Playing) return RecorderPlaybackResult::Failed;
    if (audio::pcm_playing()) return RecorderPlaybackResult::Playing;
    stopRecorderPlayback();
    return RecorderPlaybackResult::Finished;
}

void Hal::stopRecorderPlayback()
{
    if (recorder.mode != Mode::Playing) return;
    audio::stop_pcm();
    recorder.mode = Mode::Idle;
}

RecorderWaveform Hal::getRecorderWaveform() const
{
    if (recorder.mode == Mode::Capturing) return recorder.waveform;
    if (recorder.mode == Mode::Playing) return audio::pcm_waveform();
    return {};
}

namespace hal::internal {
void recorderShutdown()
{
    endSession();
    recorder = {};
}
}  // namespace hal::internal
