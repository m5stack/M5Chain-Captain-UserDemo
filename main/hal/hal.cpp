/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>
#include <mooncake_log.h>
#include <string_view>

namespace {

constexpr std::string_view Tag = "HAL";
std::unique_ptr<Hal> hal_instance;

bool isLeapYear(std::uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

std::uint8_t daysInMonth(std::uint16_t year, std::uint8_t month)
{
    static constexpr std::uint8_t MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return MonthDays[month - 1];
}

}  // namespace

Hal& GetHAL()
{
    if (!hal_instance) {
        hal_instance = std::make_unique<Hal>();
    }
    return *hal_instance;
}

bool DateTime::isValid() const
{
    const std::uint8_t max_day = daysInMonth(year, month);
    return year >= 2000 && year <= 2099 && max_day != 0 && day >= 1 && day <= max_day && hour < 24 && minute < 60 &&
           second < 60 && weekday < 7;
}

void Hal::init()
{
    mclog::tagInfo(Tag, "init");

    ESP_ERROR_CHECK(hal::internal::powerInit());
    ESP_ERROR_CHECK(hal::internal::displayBlankBacklight());

    if (!loadDeviceSettings()) {
        mclog::tagWarn(Tag, "device settings unavailable, using defaults");
    }
    if (!setTimezone(_device_settings.timezone)) {
        mclog::tagWarn(Tag, "failed to apply timezone: {}", _device_settings.timezone);
    }
    if (!initFileSystem()) {
        mclog::tagWarn(Tag, "file system unavailable; storage-backed apps will be disabled");
    }

    ESP_ERROR_CHECK(hal::internal::buttonInit());
    ESP_ERROR_CHECK(hal::internal::rtcInit());
    ESP_ERROR_CHECK(hal::internal::imuInit());
    ESP_ERROR_CHECK(hal::internal::rgbInit());
    ESP_ERROR_CHECK(hal::internal::irInit());
    ESP_ERROR_CHECK(hal::internal::displayInit());

    _display_initialized = true;
    showBootSplash();

    ESP_ERROR_CHECK(hal::internal::audioInit());
}

void Hal::delay(std::uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

std::uint32_t Hal::millis()
{
    return esp_timer_get_time() / 1000;
}

void Hal::feedTheDog()
{
    const auto now = millis();
    hal::internal::wifiUpdate(now);
    hal::internal::ezdataUpdate(now);
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    updateDisplayFps();
#endif
    vTaskDelay(1);
}

bool Hal::beginChainEzData()
{
    return hal::internal::ezdataStart();
}

void Hal::endChainEzData()
{
    hal::internal::ezdataStop();
}

EzDataStatus Hal::getChainEzDataStatus() const
{
    return hal::internal::ezdataStatus();
}

void Hal::publishChainEzDataSnapshot(const ChainSnapshot& snapshot)
{
    hal::internal::ezdataPublishChainSnapshot(snapshot);
}
