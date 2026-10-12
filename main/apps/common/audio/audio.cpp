/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <hal/hal.h>
#include <limits>
#include <mutex>
#include <vector>

namespace audio {
namespace {

constexpr float Pi                  = 3.14159265358979323846F;
constexpr std::size_t WaveTableSize = 256;
constexpr std::size_t MidiNoteCount = 128;
constexpr std::size_t VoiceCount    = 2;
constexpr std::size_t QueueCapacity = 16;
constexpr std::int32_t Q15One       = 32767;
constexpr float UiSfxGainScale      = 25.0F / 24.0F;  // volume 80 matches the current volume 100 level

enum class CommandType : std::uint8_t {
    UiSfx,
    Note,
    Tone,
    Stop,
};

enum class Waveform : std::uint8_t {
    Sine,
    Triangle,
    Pulse,
};

struct Command {
    CommandType type      = CommandType::Stop;
    UiSfx sound           = UiSfx::Navigate;
    std::uint8_t midiNote = 69;
    float frequency       = 440.0F;
    float durationSec     = 0.02F;
    float volumeScale     = 0.5F;
};

struct Voice {
    Waveform waveform             = Waveform::Sine;
    std::uint32_t phase           = 0;
    std::int64_t phaseStep        = 0;
    std::int64_t phaseStepDelta   = 0;
    std::uint32_t ageSamples      = 0;
    std::uint32_t delaySamples    = 0;
    std::uint32_t durationSamples = 0;
    std::uint32_t attackSamples   = 0;
    std::uint32_t noiseSamples    = 0;
    std::uint32_t noiseState      = 0x6D2B79F5U;
    std::int32_t gainQ15          = 0;
    std::int32_t sineMixQ15       = 0;
    std::int32_t noiseGainQ15     = 0;
    bool active                   = false;
};

std::array<std::int16_t, WaveTableSize> sine_table{};
std::array<std::uint32_t, MidiNoteCount> midi_phase_steps{};
std::array<Voice, VoiceCount> voices{};
std::array<Command, QueueCapacity> command_queue{};
std::atomic<std::size_t> queue_read{0};
std::atomic<std::size_t> queue_write{0};
std::atomic<int> output_volume_q15{Q15One};
std::atomic<bool> output_muted{false};
std::uint32_t output_sample_rate = 22050;
std::mutex pcm_mutex;
std::vector<std::int16_t> pcm_samples;
std::size_t pcm_position = 0;
PcmWaveform pcm_levels{};

std::int32_t toQ15(float value)
{
    return static_cast<std::int32_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(Q15One)));
}

std::uint32_t millisecondsToSamples(float milliseconds)
{
    return std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::lround(milliseconds * static_cast<float>(output_sample_rate) / 1000.0F)));
}

std::uint32_t frequencyToPhaseStep(float frequency)
{
    if (frequency <= 0.0F || output_sample_rate == 0) {
        return 0;
    }
    constexpr double PhaseRange = 4294967296.0;
    const double step           = static_cast<double>(frequency) * PhaseRange / static_cast<double>(output_sample_rate);
    return static_cast<std::uint32_t>(std::clamp(step, 1.0, PhaseRange - 1.0));
}

bool enqueue(const Command& command)
{
    const std::size_t write = queue_write.load(std::memory_order_relaxed);
    const std::size_t next  = (write + 1) % QueueCapacity;
    if (next == queue_read.load(std::memory_order_acquire)) {
        return false;
    }
    command_queue[write] = command;
    queue_write.store(next, std::memory_order_release);
    return true;
}

bool dequeue(Command& command)
{
    const std::size_t read = queue_read.load(std::memory_order_relaxed);
    if (read == queue_write.load(std::memory_order_acquire)) {
        return false;
    }
    command = command_queue[read];
    queue_read.store((read + 1) % QueueCapacity, std::memory_order_release);
    return true;
}

void clearVoices()
{
    for (auto& voice : voices) {
        voice.active = false;
    }
}

void startVoice(std::size_t index, Waveform waveform, std::uint32_t start_step, std::uint32_t end_step,
                float duration_ms, float delay_ms, float attack_ms, float gain, float sine_mix, float noise_ms,
                float noise_gain)
{
    if (index >= voices.size()) {
        return;
    }

    auto& voice           = voices[index];
    voice.waveform        = waveform;
    voice.phase           = 0;
    voice.phaseStep       = start_step;
    voice.ageSamples      = 0;
    voice.delaySamples    = delay_ms <= 0.0F ? 0 : millisecondsToSamples(delay_ms);
    voice.durationSamples = millisecondsToSamples(duration_ms);
    voice.attackSamples   = std::min(millisecondsToSamples(attack_ms), voice.durationSamples);
    voice.phaseStepDelta  = (static_cast<std::int64_t>(end_step) - static_cast<std::int64_t>(start_step)) /
                           static_cast<std::int64_t>(voice.durationSamples);
    voice.noiseSamples = noise_ms <= 0.0F ? 0 : millisecondsToSamples(noise_ms);
    voice.noiseState   = 0x6D2B79F5U ^ static_cast<std::uint32_t>(index * 0x9E3779B9U);
    voice.gainQ15      = toQ15(gain);
    voice.sineMixQ15   = toQ15(sine_mix);
    voice.noiseGainQ15 = toQ15(noise_gain);
    voice.active       = true;
}

std::uint32_t midiStep(std::uint8_t note)
{
    return midi_phase_steps[std::min<std::size_t>(note, midi_phase_steps.size() - 1)];
}

void startUiSfx(UiSfx sound)
{
    clearVoices();
    switch (sound) {
        case UiSfx::Navigate:
            startVoice(0, Waveform::Triangle, midiStep(88), midiStep(84), 15.0F, 0.0F, 0.8F, 0.20F * UiSfxGainScale,
                       0.90F, 0.4F, 0.025F);
            break;
        case UiSfx::Confirm:
            startVoice(0, Waveform::Triangle, midiStep(86), midiStep(91), 18.0F, 0.0F, 0.8F, 0.27F * UiSfxGainScale,
                       0.90F, 0.4F, 0.025F);
            break;
        case UiSfx::Back:
            startVoice(0, Waveform::Triangle, midiStep(83), midiStep(77), 17.0F, 0.0F, 0.8F, 0.25F * UiSfxGainScale,
                       0.92F, 0.4F, 0.02F);
            break;
        case UiSfx::Boundary:
            startVoice(0, Waveform::Triangle, midiStep(76), midiStep(69), 38.0F, 0.0F, 1.0F, 0.32F * UiSfxGainScale,
                       0.86F, 0.6F, 0.025F);
            break;
    }
}

void processCommands()
{
    Command command;
    while (dequeue(command)) {
        switch (command.type) {
            case CommandType::UiSfx:
                startUiSfx(command.sound);
                break;
            case CommandType::Note:
                clearVoices();
                startVoice(0, Waveform::Sine, midiStep(command.midiNote), midiStep(command.midiNote),
                           command.durationSec * 1000.0F, 0.0F, 1.5F, command.volumeScale, 1.0F, 0.0F, 0.0F);
                break;
            case CommandType::Tone: {
                clearVoices();
                const std::uint32_t step = frequencyToPhaseStep(command.frequency);
                startVoice(0, Waveform::Sine, step, step, command.durationSec * 1000.0F, 0.0F, 1.5F,
                           command.volumeScale, 1.0F, 0.0F, 0.0F);
                break;
            }
            case CommandType::Stop:
                clearVoices();
                break;
        }
    }
}

std::int32_t waveformSample(const Voice& voice)
{
    const std::uint32_t table_index = voice.phase >> 24U;
    const std::int32_t sine         = sine_table[table_index];
    std::int32_t primary            = sine;

    if (voice.waveform == Waveform::Triangle) {
        const std::int32_t position = static_cast<std::int32_t>(voice.phase >> 16U);
        primary                     = position < 32768 ? position * 2 - 32768 : 98303 - position * 2;
    } else if (voice.waveform == Waveform::Pulse) {
        primary = (voice.phase >> 31U) == 0 ? Q15One : -Q15One;
    }

    return (primary * (Q15One - voice.sineMixQ15) + sine * voice.sineMixQ15) >> 15;
}

std::int32_t renderVoice(Voice& voice)
{
    if (!voice.active) {
        return 0;
    }
    if (voice.delaySamples > 0) {
        --voice.delaySamples;
        return 0;
    }
    if (voice.ageSamples >= voice.durationSamples) {
        voice.active = false;
        return 0;
    }

    std::int32_t envelope = Q15One;
    if (voice.ageSamples < voice.attackSamples) {
        envelope = static_cast<std::int32_t>(voice.ageSamples * static_cast<std::uint32_t>(Q15One) /
                                             std::max<std::uint32_t>(voice.attackSamples, 1));
    } else {
        const std::uint32_t decay_samples = std::max<std::uint32_t>(voice.durationSamples - voice.attackSamples, 1U);
        const std::uint32_t remaining     = voice.durationSamples - voice.ageSamples;
        const std::int32_t linear =
            static_cast<std::int32_t>(remaining * static_cast<std::uint32_t>(Q15One) / decay_samples);
        envelope = (linear * linear) >> 15;
    }

    std::int32_t sample = waveformSample(voice);
    if (voice.ageSamples < voice.noiseSamples) {
        voice.noiseState ^= voice.noiseState << 13U;
        voice.noiseState ^= voice.noiseState >> 17U;
        voice.noiseState ^= voice.noiseState << 5U;
        const std::int32_t noise          = static_cast<std::int16_t>(voice.noiseState >> 16U);
        const std::int32_t noise_envelope = static_cast<std::int32_t>(
            (voice.noiseSamples - voice.ageSamples) * static_cast<std::uint32_t>(Q15One) / voice.noiseSamples);
        sample +=
            static_cast<std::int32_t>((static_cast<std::int64_t>(noise) * voice.noiseGainQ15 * noise_envelope) >> 30);
    }

    sample = static_cast<std::int32_t>((static_cast<std::int64_t>(sample) * envelope * voice.gainQ15) >> 30);
    voice.phase += static_cast<std::uint32_t>(voice.phaseStep);
    voice.phaseStep += voice.phaseStepDelta;
    ++voice.ageSamples;
    return sample;
}

}  // namespace

void initialize(std::uint32_t sampleRate)
{
    output_sample_rate = std::max<std::uint32_t>(sampleRate, 8000);
    for (std::size_t index = 0; index < sine_table.size(); ++index) {
        const float phase = static_cast<float>(index) * 2.0F * Pi / static_cast<float>(sine_table.size());
        sine_table[index] = static_cast<std::int16_t>(std::lround(std::sin(phase) * static_cast<float>(Q15One)));
    }
    for (std::size_t note = 0; note < midi_phase_steps.size(); ++note) {
        const float frequency  = 440.0F * std::pow(2.0F, (static_cast<float>(note) - 69.0F) / 12.0F);
        midi_phase_steps[note] = frequencyToPhaseStep(frequency);
    }
    queue_read.store(0, std::memory_order_relaxed);
    queue_write.store(0, std::memory_order_relaxed);
    clearVoices();
}

void shutdown()
{
    clearVoices();
    stop_pcm();
    queue_read.store(0, std::memory_order_relaxed);
    queue_write.store(0, std::memory_order_relaxed);
}

void set_output_volume(int volume)
{
    output_volume_q15.store(toQ15(static_cast<float>(std::clamp(volume, 0, 100)) / 100.0F), std::memory_order_relaxed);
}

void set_output_muted(bool muted)
{
    output_muted.store(muted, std::memory_order_relaxed);
}

void play_ui_sfx(UiSfx sound)
{
    const auto& hal = GetHAL();
    if (!hal.deviceSettings().buttonSfxEnabled || hal.getSpeakerVolume() <= 0 || hal.isSpeakerMuted()) {
        return;
    }
    Command command;
    command.type  = CommandType::UiSfx;
    command.sound = sound;
    enqueue(command);
}

void play_note(std::uint8_t midiNote, float durationSec, float volumeScale)
{
    if (durationSec <= 0.0F || volumeScale <= 0.0F || GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    Command command;
    command.type        = CommandType::Note;
    command.midiNote    = std::min<std::uint8_t>(midiNote, 127);
    command.durationSec = durationSec;
    command.volumeScale = std::min(volumeScale, 1.0F);
    enqueue(command);
}

void play_tone(int frequency, float durationSec, float volumeScale)
{
    if (frequency <= 0 || durationSec <= 0.0F || volumeScale <= 0.0F || GetHAL().getSpeakerVolume() <= 0) {
        return;
    }
    Command command;
    command.type        = CommandType::Tone;
    command.frequency   = static_cast<float>(frequency);
    command.durationSec = durationSec;
    command.volumeScale = std::min(volumeScale, 1.0F);
    enqueue(command);
}

void stop()
{
    Command command;
    command.type = CommandType::Stop;
    enqueue(command);
}

void play_pcm(const std::vector<std::int16_t>& samples)
{
    // Copy before taking the callback lock, so allocation does not block rendering.
    auto copy = samples;
    std::lock_guard<std::mutex> lock(pcm_mutex);
    pcm_samples.swap(copy);
    pcm_position = 0;
    pcm_levels.fill(0);
}

void stop_pcm()
{
    std::vector<std::int16_t> retired;
    {
        std::lock_guard<std::mutex> lock(pcm_mutex);
        retired.swap(pcm_samples);
        pcm_position = 0;
        pcm_levels.fill(0);
    }
}

bool pcm_playing()
{
    std::lock_guard<std::mutex> lock(pcm_mutex);
    return pcm_position < pcm_samples.size();
}

PcmWaveform pcm_waveform()
{
    std::lock_guard<std::mutex> lock(pcm_mutex);
    return pcm_levels;
}

void render(std::int16_t* output, std::size_t sampleCount)
{
    if (output == nullptr || sampleCount == 0) {
        return;
    }

    processCommands();
    const bool muted        = output_muted.load(std::memory_order_relaxed);
    const int master_volume = output_volume_q15.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(pcm_mutex);
    pcm_levels.fill(0);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        std::int32_t mixed = 0;
        for (auto& voice : voices) {
            mixed += renderVoice(voice);
        }
        if (pcm_position < pcm_samples.size()) {
            const std::int16_t pcm_sample = pcm_samples[pcm_position++];
            mixed += pcm_sample;
            const std::size_t point = std::min(index * pcm_levels.size() / sampleCount, pcm_levels.size() - 1);
            const std::int32_t magnitude =
                std::min<std::int32_t>(std::abs(static_cast<std::int32_t>(pcm_sample)), 8192);
            pcm_levels[point] =
                std::max<std::uint8_t>(pcm_levels[point], static_cast<std::uint8_t>(magnitude * 255 / 8192));
        }
        if (muted) {
            mixed = 0;
        } else {
            mixed = static_cast<std::int32_t>((static_cast<std::int64_t>(mixed) * master_volume) >> 15);
        }
        output[index] = static_cast<std::int16_t>(
            std::clamp(mixed, static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
                       static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())));
    }
}

}  // namespace audio
