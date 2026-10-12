/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <hal/hal.h>

namespace ir_copy {

constexpr std::size_t SlotCount                 = 5;
constexpr std::size_t MaximumPulseCount         = IrMaximumPulseCount;
constexpr std::uint32_t CaptureTimeoutMs        = 6000;
constexpr std::uint32_t DefaultCarrierFrequency = IrDefaultCarrierFrequency;
constexpr std::uint16_t DefaultDutyPermille     = IrDefaultDutyPermille;

using Pulse  = IrRawPulse;
using Signal = IrRawSignal;

}  // namespace ir_copy
