/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstdint>

class M5IOE1;
class M5PM1;
struct ChainSnapshot;
struct EzDataStatus;

namespace hal::internal {

esp_err_t powerInit();
esp_err_t buttonInit();
esp_err_t rtcInit();
esp_err_t imuInit();
esp_err_t rgbInit();
esp_err_t irInit();
esp_err_t displayBlankBacklight();
esp_err_t displayInit();
esp_err_t audioInit();
esp_err_t fsInit();
void wifiUpdate(std::uint32_t now);
bool ezdataStart();
void ezdataStop();
void ezdataUpdate(std::uint32_t now);
void ezdataPublishChainSnapshot(const ChainSnapshot& snapshot);
EzDataStatus ezdataStatus();

bool audioIsReady();
bool audioSetCapturePath(bool capturing);

i2c_master_bus_handle_t i2cBus();
M5PM1* pmic();
M5IOE1* ioe();

}  // namespace hal::internal
