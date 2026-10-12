/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <apps/common/audio/audio.h>
#include <mooncake_log.h>

namespace {

constexpr std::string_view Tag = "HAL-AUDIO";
SDL_AudioDeviceID audio_device = 0;
bool owns_audio_subsystem      = false;

void fillAudioBuffer(void*, Uint8* stream, int byteCount)
{
    if (stream == nullptr || byteCount <= 0) {
        return;
    }
    SDL_memset(stream, 0, static_cast<std::size_t>(byteCount));
    audio::render(reinterpret_cast<std::int16_t*>(stream), static_cast<std::size_t>(byteCount) / sizeof(std::int16_t));
}

}  // namespace

namespace hal::internal {

bool audioInit()
{
    if (audio_device != 0) {
        return true;
    }

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            mclog::tagError(Tag, "failed to initialize SDL audio: {}", SDL_GetError());
            return false;
        }
        owns_audio_subsystem = true;
    }

    SDL_AudioSpec desired = {};
    desired.freq          = static_cast<int>(GetHAL().getAudioSampleRate());
    desired.format        = AUDIO_S16SYS;
    desired.channels      = 1;
    desired.samples       = 128;
    desired.callback      = fillAudioBuffer;

    audio::initialize(GetHAL().getAudioSampleRate());
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (audio_device == 0) {
        mclog::tagError(Tag, "failed to open SDL audio device: {}", SDL_GetError());
        audio::shutdown();
        if (owns_audio_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            owns_audio_subsystem = false;
        }
        return false;
    }

    audio::set_output_volume(GetHAL().getSpeakerVolume());
    audio::set_output_muted(GetHAL().isSpeakerMuted());
    SDL_PauseAudioDevice(audio_device, 0);
    mclog::tagInfo(Tag, "SDL audio started: {} Hz, mono, 128 frames", desired.freq);
    return true;
}

void audioShutdown()
{
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 1);
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    audio::shutdown();
    if (owns_audio_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        owns_audio_subsystem = false;
    }
}

}  // namespace hal::internal

bool Hal::setAudioSampleRate(std::uint32_t sampleRate)
{
    if (sampleRate < 8000 || sampleRate > 48000) {
        return false;
    }
    _audio_sample_rate = sampleRate;
    return true;
}

bool Hal::setSpeakerVolume(int volume)
{
    _speaker_volume = std::clamp(volume, 0, 100);
    audio::set_output_volume(_speaker_volume);
    return true;
}

bool Hal::setSpeakerMuted(bool muted)
{
    _speaker_muted = muted;
    audio::set_output_muted(muted);
    return true;
}

void Hal::audioPlay(std::vector<std::int16_t>& samples, bool async)
{
    audio::play_pcm(samples);
    (void)async;
}

void Hal::audioStop()
{
    audio::stop_pcm();
}

bool Hal::audioWrite(const std::int16_t* samples, std::size_t sampleCount)
{
    return samples != nullptr || sampleCount == 0;
}

bool Hal::audioRead(std::int16_t* samples, std::size_t sampleCount)
{
    return samples != nullptr || sampleCount == 0;
}
