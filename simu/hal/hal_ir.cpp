/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>
#include <mooncake_log.h>
#include <string_view>
#include <unordered_map>

namespace {

constexpr std::string_view Tag                  = "HAL-IR";
constexpr std::uint32_t SimulatedCaptureDelayMs = 900;

std::unordered_map<std::size_t, IrRawSignal> stored_signals;

bool isValidSignal(const IrRawSignal& signal)
{
    if (signal.carrierFrequency == 0 || signal.dutyPermille == 0 || signal.dutyPermille > 1000 ||
        signal.pulses.empty() || signal.pulses.size() > IrMaximumPulseCount) {
        return false;
    }

    return std::all_of(signal.pulses.begin(), signal.pulses.end(),
                       [](const IrRawPulse& pulse) { return pulse.durationUs != 0; });
}

}  // namespace

bool Hal::sendIrNec(std::uint16_t, std::uint16_t)
{
    return _ir_power_enabled;
}

bool Hal::beginIrRaw()
{
    if (!_ir_power_enabled) {
        return false;
    }

    _ir_raw_initialized = true;
    mclog::tagInfo(Tag, "raw IR ready");
    return true;
}

void Hal::endIrRaw()
{
    cancelIrRawCapture();
    if (_ir_raw_initialized) {
        mclog::tagInfo(Tag, "raw IR stopped");
    }
    _ir_raw_initialized = false;
}

bool Hal::startIrRawCapture(std::uint32_t timeoutMs)
{
    if (!_ir_power_enabled || !_ir_raw_initialized || _ir_raw_capture_active || timeoutMs == 0) {
        return false;
    }

    _ir_raw_capture_started_at = millis();
    _ir_raw_capture_timeout_ms = timeoutMs;
    _ir_raw_capture_active     = true;
    return true;
}

IrRawCaptureResult Hal::pollIrRawCapture(IrRawSignal& signal)
{
    if (!_ir_power_enabled || !_ir_raw_initialized || !_ir_raw_capture_active) {
        return IrRawCaptureResult::Failed;
    }

    const std::uint32_t elapsed      = millis() - _ir_raw_capture_started_at;
    const std::uint32_t result_delay = std::min(SimulatedCaptureDelayMs, _ir_raw_capture_timeout_ms);
    if (elapsed < result_delay) {
        return IrRawCaptureResult::Pending;
    }

    _ir_raw_capture_active = false;
    if (_ir_raw_capture_timeout_ms < SimulatedCaptureDelayMs) {
        return IrRawCaptureResult::Timeout;
    }

    signal.carrierFrequency = IrDefaultCarrierFrequency;
    signal.dutyPermille     = IrDefaultDutyPermille;
    signal.pulses           = {
        {true, 9000}, {false, 4500}, {true, 560}, {false, 560}, {true, 560}, {false, 1690}, {true, 560}, {false, 560},
    };
    return IrRawCaptureResult::Success;
}

void Hal::cancelIrRawCapture()
{
    _ir_raw_capture_active = false;
}

bool Hal::sendIrRaw(const IrRawSignal& signal)
{
    if (!_ir_power_enabled || !_ir_raw_initialized || _ir_raw_capture_active || !isValidSignal(signal)) {
        return false;
    }

    mclog::tagInfo(Tag, "sent simulated raw signal with {} pulses", signal.pulses.size());
    return true;
}

bool Hal::loadIrRawSignal(std::size_t slot, IrRawSignal& signal)
{
    const auto found = stored_signals.find(slot);
    if (found == stored_signals.end()) {
        return false;
    }
    signal = found->second;
    return true;
}

bool Hal::saveIrRawSignal(std::size_t slot, const IrRawSignal& signal)
{
    if (!isValidSignal(signal)) {
        return false;
    }
    stored_signals[slot] = signal;
    return true;
}
