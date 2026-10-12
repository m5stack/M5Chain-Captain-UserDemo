/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

inline constexpr std::size_t PcmWaveformPointCount = 9;
using PcmWaveform                                  = std::array<std::uint8_t, PcmWaveformPointCount>;

enum class UiSfx : std::uint8_t {
    Navigate,
    Confirm,
    Back,
    Boundary,
};

void initialize(std::uint32_t sampleRate);
void shutdown();
void set_output_volume(int volume);
void set_output_muted(bool muted);

void play_ui_sfx(UiSfx sound);
void play_note(std::uint8_t midiNote, float durationSec = 0.03F, float volumeScale = 0.5F);
void play_tone(int frequency, float durationSec = 0.02F, float volumeScale = 0.5F);
void stop();
void play_pcm(const std::vector<std::int16_t>& samples);
void stop_pcm();
bool pcm_playing();
PcmWaveform pcm_waveform();

void render(std::int16_t* output, std::size_t sampleCount);

}  // namespace audio
