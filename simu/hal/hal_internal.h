/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

struct ChainSnapshot;
struct EzDataStatus;

namespace hal::internal {

bool ezdataStart();
void ezdataStop();
void ezdataUpdate(std::uint32_t now);
void ezdataPublishChainSnapshot(const ChainSnapshot& snapshot);
EzDataStatus ezdataStatus();

}  // namespace hal::internal
