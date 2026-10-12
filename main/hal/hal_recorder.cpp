/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <apps/common/audio/audio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace {

constexpr char Tag[]                    = "HAL-RECORDER";
constexpr std::size_t CaptureChunkCount = 512;
constexpr std::size_t PrerollCount      = 1024;
constexpr std::uint32_t TrimStartMs     = 120;
constexpr std::uint32_t TrimEndMs       = 160;

// Voice-focused capture curve. The high-pass filter reduces breath and handling
// rumble, while compression lifts speech without allowing close-range air noise
// to dominate the recording.
constexpr float HighPassCutoffHz     = 120.0F;
constexpr float CompressorThreshold  = 0.02F;
constexpr float CompressorMakeupGain = 22.4F;
constexpr float CompressorCeiling    = 0.46F;
constexpr float CompressorAttackMs   = 4.0F;
constexpr float CompressorReleaseMs  = 160.0F;
constexpr float LimiterPeak          = 0.46F;

enum class Mode {
    Idle,
    Capturing,
    Playing,
};

struct VoiceProcessor {
    float highPassAlpha  = 0.0F;
    float attackAlpha    = 0.0F;
    float releaseAlpha   = 0.0F;
    float previousInput  = 0.0F;
    float previousOutput = 0.0F;
    float envelope       = 0.0F;
    float gain           = CompressorMakeupGain;

    void reset(std::uint32_t sampleRate)
    {
        constexpr float Pi = 3.14159265358979323846F;
        const float rate   = static_cast<float>(sampleRate);
        highPassAlpha      = std::exp(-2.0F * Pi * HighPassCutoffHz / rate);
        attackAlpha        = std::exp(-1.0F / (rate * CompressorAttackMs / 1000.0F));
        releaseAlpha       = std::exp(-1.0F / (rate * CompressorReleaseMs / 1000.0F));
        previousInput      = 0.0F;
        previousOutput     = 0.0F;
        envelope           = 0.0F;
        gain               = CompressorMakeupGain;
    }

    void process(std::int16_t* samples, std::size_t count)
    {
        constexpr float SampleScale = 1.0F / 32768.0F;
        for (std::size_t index = 0; index < count; ++index) {
            const float input    = static_cast<float>(samples[index]) * SampleScale;
            const float filtered = highPassAlpha * (previousOutput + input - previousInput);
            previousInput        = input;
            previousOutput       = filtered;

            const float magnitude     = std::abs(filtered);
            const float envelopeAlpha = magnitude > envelope ? attackAlpha : releaseAlpha;
            envelope                  = envelopeAlpha * envelope + (1.0F - envelopeAlpha) * magnitude;

            float targetGain = CompressorMakeupGain;
            if (envelope > CompressorThreshold) {
                targetGain = CompressorCeiling / envelope;
            }

            // Pull gain down quickly for loud air bursts, then return slowly so
            // the noise floor does not pump between spoken syllables.
            const float gainAlpha = targetGain < gain ? attackAlpha : releaseAlpha;
            gain                  = gainAlpha * gain + (1.0F - gainAlpha) * targetGain;

            const float peakSafeGain = magnitude > 0.0F ? LimiterPeak / magnitude : CompressorMakeupGain;
            const float output       = filtered * std::min(gain, peakSafeGain);
            samples[index] = static_cast<std::int16_t>(std::clamp(std::lround(output * 32767.0F), -32768L, 32767L));
        }
    }
};

struct RecorderState {
    std::array<std::vector<std::int16_t>, RecorderSlotCount> clips;
    std::vector<std::int16_t> pending;
    RecorderWaveform waveform{};
    VoiceProcessor processor;
    std::size_t slot         = 0;
    std::size_t sampleLimit  = 0;
    Mode mode                = Mode::Idle;
    bool initialized         = false;
    bool stopRequested       = false;
    bool captureComplete     = false;
    bool captureFailed       = false;
    TaskHandle_t captureTask = nullptr;
};

RecorderState recorder;
std::mutex recorderMutex;

void clearPending()
{
    std::vector<std::int16_t>{}.swap(recorder.pending);
    recorder.waveform.fill(0);
}

bool trimButtonNoise(std::uint32_t sampleRate)
{
    const std::size_t trimStart = static_cast<std::size_t>(sampleRate) * TrimStartMs / 1000;
    const std::size_t trimEnd   = static_cast<std::size_t>(sampleRate) * TrimEndMs / 1000;
    if (recorder.pending.size() <= trimStart + trimEnd) {
        return false;
    }

    const auto audioEnd = recorder.pending.end() - static_cast<std::ptrdiff_t>(trimEnd);
    const auto newEnd   = std::move(recorder.pending.begin() + static_cast<std::ptrdiff_t>(trimStart), audioEnd,
                                    recorder.pending.begin());
    recorder.pending.erase(newEnd, recorder.pending.end());
    return true;
}

void updateWaveform(const std::int16_t* samples, std::size_t count)
{
    if (samples == nullptr || count == 0) {
        recorder.waveform.fill(0);
        return;
    }

    for (std::size_t point = 0; point < recorder.waveform.size(); ++point) {
        const std::size_t begin = point * count / recorder.waveform.size();
        const std::size_t end   = std::min(count, std::max(begin + 1, (point + 1) * count / recorder.waveform.size()));
        int peak                = 0;
        for (std::size_t index = begin; index < end; ++index) {
            peak = std::max(peak, std::abs(static_cast<int>(samples[index])));
        }
        const float normalized   = static_cast<float>(std::min(peak, 8192)) / 8192.0F;
        recorder.waveform[point] = static_cast<std::uint8_t>(std::lround(std::sqrt(normalized) * 255.0F));
    }
}

bool restorePlaybackPath()
{
    if (hal::internal::audioSetCapturePath(false)) {
        return true;
    }
    ESP_LOGE(Tag, "failed to restore speaker path after capture");
    return false;
}

void captureTask(void*)
{
    std::array<std::int16_t, CaptureChunkCount> samples{};
    while (true) {
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(recorderMutex);
            if (recorder.stopRequested || recorder.pending.size() >= recorder.sampleLimit) {
                recorder.captureComplete = recorder.pending.size() >= recorder.sampleLimit;
                break;
            }
            count = std::min(samples.size(), recorder.sampleLimit - recorder.pending.size());
        }

        if (!GetHAL().audioRead(samples.data(), count)) {
            ESP_LOGE(Tag, "microphone read failed");
            std::lock_guard<std::mutex> lock(recorderMutex);
            recorder.captureFailed = true;
            break;
        }
        recorder.processor.process(samples.data(), count);

        std::lock_guard<std::mutex> lock(recorderMutex);
        if (!recorder.stopRequested) {
            recorder.pending.insert(recorder.pending.end(), samples.begin(), samples.begin() + count);
            updateWaveform(samples.data(), count);
            recorder.captureComplete = recorder.pending.size() >= recorder.sampleLimit;
        }
    }

    {
        std::lock_guard<std::mutex> lock(recorderMutex);
        recorder.captureTask = nullptr;
    }
    vTaskDelete(nullptr);
}

void stopCaptureTask()
{
    {
        std::lock_guard<std::mutex> lock(recorderMutex);
        recorder.stopRequested = true;
    }
    while (true) {
        {
            std::lock_guard<std::mutex> lock(recorderMutex);
            if (recorder.captureTask == nullptr) {
                return;
            }
        }
        vTaskDelay(1);
    }
}

}  // namespace

bool Hal::beginRecorder()
{
    if (!hal::internal::audioIsReady()) {
        ESP_LOGE(Tag, "audio codec is unavailable");
        return false;
    }

    cancelRecorderCapture();
    stopRecorderPlayback();
    recorder.initialized = true;
    ESP_LOGI(Tag, "recorder ready: %" PRIu32 " Hz, mono, %" PRIu32 " second limit, %u KiB PSRAM free",
             _audio_sample_rate, RecorderMaxSeconds,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return true;
}

void Hal::endRecorder()
{
    cancelRecorderCapture();
    stopRecorderPlayback();
    recorder.initialized = false;
    clearPending();
    // Clips intentionally survive closing the app, but never touch disk.
}

bool Hal::hasRecorderClip(std::size_t slot) const
{
    return slot < recorder.clips.size() && !recorder.clips[slot].empty();
}

bool Hal::startRecorderCapture(std::size_t slot)
{
    if (!recorder.initialized || slot >= recorder.clips.size() || recorder.mode == Mode::Capturing) {
        return false;
    }

    stopRecorderPlayback();
    audio::stop();
    audio::stop_pcm();
    if (!hal::internal::audioSetCapturePath(true)) {
        ESP_LOGE(Tag, "failed to prepare microphone path");
        return false;
    }

    std::array<std::int16_t, PrerollCount> preroll{};
    if (!audioRead(preroll.data(), preroll.size())) {
        ESP_LOGE(Tag, "failed to discard microphone preroll");
        restorePlaybackPath();
        return false;
    }

    clearPending();
    recorder.slot                   = slot;
    recorder.sampleLimit            = static_cast<std::size_t>(_audio_sample_rate) * RecorderMaxSeconds;
    const std::size_t requiredBytes = recorder.sampleLimit * sizeof(std::int16_t);
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < requiredBytes) {
        ESP_LOGE(Tag, "not enough contiguous PSRAM for %u-byte recording", static_cast<unsigned>(requiredBytes));
        restorePlaybackPath();
        return false;
    }
    recorder.pending.reserve(recorder.sampleLimit);
    recorder.processor.reset(_audio_sample_rate);
    recorder.mode            = Mode::Capturing;
    recorder.stopRequested   = false;
    recorder.captureComplete = false;
    recorder.captureFailed   = false;
    if (xTaskCreate(captureTask, "audio_capture", 4 * 1024, nullptr, 5, &recorder.captureTask) != pdPASS) {
        recorder.captureTask = nullptr;
        recorder.mode        = Mode::Idle;
        clearPending();
        restorePlaybackPath();
        ESP_LOGE(Tag, "failed to create microphone capture task");
        return false;
    }
    ESP_LOGI(Tag, "capture started in slot %u", static_cast<unsigned>(slot + 1));
    return true;
}

RecorderCaptureResult Hal::pollRecorderCapture()
{
    std::lock_guard<std::mutex> lock(recorderMutex);
    if (recorder.mode != Mode::Capturing || recorder.captureFailed) {
        return RecorderCaptureResult::Failed;
    }
    return recorder.captureComplete ? RecorderCaptureResult::Complete : RecorderCaptureResult::Recording;
}

bool Hal::finishRecorderCapture()
{
    if (recorder.mode != Mode::Capturing) {
        return false;
    }

    stopCaptureTask();
    bool captured = false;
    {
        std::lock_guard<std::mutex> lock(recorderMutex);
        captured = !recorder.captureFailed && trimButtonNoise(_audio_sample_rate);
        if (captured) {
            recorder.clips[recorder.slot].swap(recorder.pending);
            ESP_LOGI(Tag, "saved %u samples in slot %u", static_cast<unsigned>(recorder.clips[recorder.slot].size()),
                     static_cast<unsigned>(recorder.slot + 1));
        }
        recorder.mode = Mode::Idle;
    }
    clearPending();
    restorePlaybackPath();
    return captured;
}

void Hal::cancelRecorderCapture()
{
    if (recorder.mode != Mode::Capturing) {
        return;
    }

    stopCaptureTask();
    recorder.mode = Mode::Idle;
    clearPending();
    restorePlaybackPath();
    ESP_LOGI(Tag, "capture cancelled");
}

bool Hal::startRecorderPlayback(std::size_t slot)
{
    if (!recorder.initialized || recorder.mode == Mode::Capturing || !hasRecorderClip(slot)) {
        return false;
    }

    stopRecorderPlayback();
    audio::play_pcm(recorder.clips[slot]);
    recorder.slot = slot;
    recorder.mode = Mode::Playing;
    ESP_LOGI(Tag, "playback started for slot %u", static_cast<unsigned>(slot + 1));
    return true;
}

RecorderPlaybackResult Hal::pollRecorderPlayback()
{
    if (recorder.mode != Mode::Playing) {
        return RecorderPlaybackResult::Failed;
    }
    if (audio::pcm_playing()) {
        return RecorderPlaybackResult::Playing;
    }

    stopRecorderPlayback();
    return RecorderPlaybackResult::Finished;
}

void Hal::stopRecorderPlayback()
{
    if (recorder.mode != Mode::Playing) {
        return;
    }

    audio::stop_pcm();
    recorder.mode = Mode::Idle;
}

RecorderWaveform Hal::getRecorderWaveform() const
{
    if (recorder.mode == Mode::Capturing) {
        std::lock_guard<std::mutex> lock(recorderMutex);
        return recorder.waveform;
    }
    if (recorder.mode == Mode::Playing) {
        return audio::pcm_waveform();
    }
    return {};
}
