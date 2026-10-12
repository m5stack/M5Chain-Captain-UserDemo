/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {

std::time_t rtc_time                             = std::time(nullptr);
std::chrono::steady_clock::time_point rtc_set_at = std::chrono::steady_clock::now();
constexpr std::size_t MaximumTimezoneLength      = 63;

}  // namespace

bool Hal::setTimezone(std::string_view timezone)
{
    if (timezone.empty() || timezone.size() > MaximumTimezoneLength) {
        return false;
    }
    const std::string value(timezone);
    if (setenv("TZ", value.c_str(), 1) != 0) {
        return false;
    }
    tzset();
    return true;
}

bool Hal::getDateTime(DateTime& dateTime)
{
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - rtc_set_at);
    const std::time_t current_time = rtc_time + elapsed.count();
    std::tm value                  = {};
    if (localtime_r(&current_time, &value) == nullptr) {
        return false;
    }
    dateTime.year    = static_cast<std::uint16_t>(value.tm_year + 1900);
    dateTime.month   = static_cast<std::uint8_t>(value.tm_mon + 1);
    dateTime.day     = static_cast<std::uint8_t>(value.tm_mday);
    dateTime.hour    = static_cast<std::uint8_t>(value.tm_hour);
    dateTime.minute  = static_cast<std::uint8_t>(value.tm_min);
    dateTime.second  = static_cast<std::uint8_t>(value.tm_sec);
    dateTime.weekday = static_cast<std::uint8_t>(value.tm_wday);
    return dateTime.isValid();
}

bool Hal::setDateTime(const DateTime& dateTime)
{
    if (!dateTime.isValid()) {
        return false;
    }
    std::tm value            = {};
    value.tm_year            = dateTime.year - 1900;
    value.tm_mon             = dateTime.month - 1;
    value.tm_mday            = dateTime.day;
    value.tm_hour            = dateTime.hour;
    value.tm_min             = dateTime.minute;
    value.tm_sec             = dateTime.second;
    value.tm_isdst           = -1;
    const std::time_t result = std::mktime(&value);
    if (result < 0) {
        return false;
    }
    rtc_time   = result;
    rtc_set_at = std::chrono::steady_clock::now();
    return true;
}
