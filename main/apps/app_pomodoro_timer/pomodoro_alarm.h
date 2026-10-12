/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

class PomodoroAlarm {
public:
    void start(std::uint32_t now);
    void update(std::uint32_t now);
    void stop();

    bool isActive() const
    {
        return _active;
    }

private:
    static constexpr std::uint32_t ToneDurationMs  = 70U;
    static constexpr std::uint32_t ToneIntervalMs  = 180U;
    static constexpr std::uint32_t CycleIntervalMs = 720U;
    static constexpr std::uint8_t TonesPerCycle    = 4U;

    bool _active               = false;
    std::uint8_t _tonesInCycle = 0;
    std::uint32_t _nextToneAt  = 0;
    bool _restoreMuted         = false;
};
