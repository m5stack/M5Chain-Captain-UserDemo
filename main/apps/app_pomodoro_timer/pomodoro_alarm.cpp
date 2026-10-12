/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "pomodoro_alarm.h"

#include <apps/common/audio/audio.h>
#include <hal/hal.h>

namespace {

bool timeReached(std::uint32_t now, std::uint32_t target)
{
    return static_cast<std::int32_t>(now - target) >= 0;
}

}  // namespace

void PomodoroAlarm::start(std::uint32_t now)
{
    if (_active) {
        return;
    }

    _active       = true;
    _tonesInCycle = 0;
    _nextToneAt   = now;
    _restoreMuted = GetHAL().isSpeakerMuted() && GetHAL().getSpeakerVolume() > 0;
    if (_restoreMuted) {
        GetHAL().setSpeakerMuted(false);
    }
    update(now);
}

void PomodoroAlarm::update(std::uint32_t now)
{
    if (!_active) {
        return;
    }

    if (!timeReached(now, _nextToneAt)) {
        return;
    }

    audio::play_tone(1760, static_cast<float>(ToneDurationMs) / 1000.0F);
    ++_tonesInCycle;
    const std::uint32_t interval = _tonesInCycle == TonesPerCycle ? CycleIntervalMs : ToneIntervalMs;
    if (_tonesInCycle == TonesPerCycle) {
        _tonesInCycle = 0;
    }
    _nextToneAt = now + interval;
}

void PomodoroAlarm::stop()
{
    if (!_active) {
        return;
    }

    _active       = false;
    _tonesInCycle = 0;
    if (_restoreMuted) {
        GetHAL().setSpeakerMuted(true);
        _restoreMuted = false;
    }
    audio::stop();
}
