/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <esp_log.h>
#include <string>

namespace {

constexpr char Tag[]                        = "HAL-RTC";
constexpr std::uint8_t RtcAddress           = 0x32;
constexpr std::uint8_t TimeReg              = 0x10;
constexpr std::uint8_t FlagReg              = 0x1d;
constexpr std::uint8_t ControlReg           = 0x1e;
constexpr std::uint8_t StopBit              = 1U << 6;
constexpr std::uint8_t VoltageLowFlag       = 1U << 1;
constexpr int I2cTimeoutMs                  = 100;
constexpr std::size_t MaximumTimezoneLength = 63;

i2c_master_dev_handle_t rtc_device = nullptr;

std::uint8_t bcdToDecimal(std::uint8_t value)
{
    return static_cast<std::uint8_t>((value >> 4) * 10 + (value & 0x0f));
}

std::uint8_t decimalToBcd(std::uint8_t value)
{
    return static_cast<std::uint8_t>((value / 10) << 4 | value % 10);
}

bool isBcd(std::uint8_t value)
{
    return (value >> 4) <= 9 && (value & 0x0f) <= 9;
}

bool isSupportedWeekday(std::uint8_t value)
{
    value &= 0x7f;
    if (value <= 6) {
        return true;
    }
    return value <= 0x40 && (value & (value - 1U)) == 0;
}

std::uint8_t yearFromRegister(std::uint8_t value)
{
    if (!isBcd(value) && (value & 0x80U) != 0U && isBcd(value & 0x7fU)) {
        value &= 0x7fU;
    }
    return isBcd(value) ? bcdToDecimal(value) : 0xff;
}

bool getFirmwareBuildTime(DateTime& dateTime)
{
    constexpr std::array<const char*, 12> MonthNames = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    char month_name[4] = {};
    unsigned day       = 0;
    unsigned year      = 0;
    unsigned hour      = 0;
    unsigned minute    = 0;
    unsigned second    = 0;
    if (std::sscanf(__DATE__, "%3s %u %u", month_name, &day, &year) != 3 ||
        std::sscanf(__TIME__, "%u:%u:%u", &hour, &minute, &second) != 3) {
        return false;
    }

    unsigned month = 0;
    while (month < MonthNames.size() && std::strncmp(month_name, MonthNames[month], 3) != 0) {
        ++month;
    }
    if (month == MonthNames.size()) {
        return false;
    }

    DateTime value = {
        static_cast<std::uint16_t>(year),
        static_cast<std::uint8_t>(month + 1),
        static_cast<std::uint8_t>(day),
        static_cast<std::uint8_t>(hour),
        static_cast<std::uint8_t>(minute),
        static_cast<std::uint8_t>(second),
        0,
    };
    if (!value.isValid()) {
        return false;
    }
    dateTime = value;
    return true;
}

std::int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era                 = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era    = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year    = (153U * adjusted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era     = year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

std::time_t utcTimestamp(const DateTime& value)
{
    constexpr std::int64_t SecondsPerDay = 24 * 60 * 60;
    const std::int64_t seconds           = daysFromCivil(value.year, value.month, value.day) * SecondsPerDay +
                                 value.hour * 60 * 60 + value.minute * 60 + value.second;
    return static_cast<std::time_t>(seconds);
}

DateTime dateTimeFromTm(const std::tm& value)
{
    return {
        static_cast<std::uint16_t>(value.tm_year + 1900), static_cast<std::uint8_t>(value.tm_mon + 1),
        static_cast<std::uint8_t>(value.tm_mday),         static_cast<std::uint8_t>(value.tm_hour),
        static_cast<std::uint8_t>(value.tm_min),          static_cast<std::uint8_t>(value.tm_sec),
        static_cast<std::uint8_t>(value.tm_wday),
    };
}

esp_err_t readRegisters(std::uint8_t reg, void* data, std::size_t size)
{
    return i2c_master_transmit_receive(rtc_device, &reg, 1, static_cast<std::uint8_t*>(data), size, I2cTimeoutMs);
}

esp_err_t writeRegisters(std::uint8_t reg, const void* data, std::size_t size)
{
    if (size > 7) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::array<std::uint8_t, 8> buffer = {};
    buffer[0]                          = reg;
    std::memcpy(buffer.data() + 1, data, size);
    return i2c_master_transmit(rtc_device, buffer.data(), size + 1, I2cTimeoutMs);
}

esp_err_t readDateTime(DateTime& dateTime, std::array<std::uint8_t, 7>* raw_data = nullptr)
{
    if (rtc_device == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    std::array<std::uint8_t, 7> data = {};
    const esp_err_t result           = readRegisters(TimeReg, data.data(), data.size());
    if (result != ESP_OK) {
        return result;
    }
    if (raw_data != nullptr) {
        *raw_data = data;
    }

    const std::uint8_t second = data[0] & 0x7f;
    const std::uint8_t minute = data[1] & 0x7f;
    const std::uint8_t hour   = data[2] & 0x3f;
    const std::uint8_t day    = data[4] & 0x3f;
    const std::uint8_t month  = data[5] & 0x1f;
    const std::uint8_t year   = yearFromRegister(data[6]);
    if (!isBcd(second) || !isBcd(minute) || !isBcd(hour) || !isSupportedWeekday(data[3]) || !isBcd(day) ||
        !isBcd(month) || year == 0xff) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    DateTime value;
    value.second = bcdToDecimal(second);
    value.minute = bcdToDecimal(minute);
    value.hour   = bcdToDecimal(hour);
    // Only validate WEEK here; localtime_r() recalculates it from the calendar date below.
    value.weekday = 0;
    value.day     = bcdToDecimal(day);
    value.month   = bcdToDecimal(month);
    value.year    = static_cast<std::uint16_t>(2000 + year);
    if (!value.isValid()) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const std::time_t timestamp = utcTimestamp(value);
    std::tm local_time          = {};
    if (localtime_r(&timestamp, &local_time) == nullptr) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    value = dateTimeFromTm(local_time);
    if (!value.isValid()) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    dateTime = value;
    return ESP_OK;
}

}  // namespace

namespace hal::internal {

esp_err_t rtcInit()
{
    if (i2cBus() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_device_config_t config = {};
    config.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    config.device_address      = RtcAddress;
    config.scl_speed_hz        = 400000;
    const esp_err_t result     = i2c_master_bus_add_device(i2cBus(), &config, &rtc_device);
    if (result != ESP_OK) {
        ESP_LOGE(Tag, "RX8130 initialization failed: %s", esp_err_to_name(result));
        return result;
    }

    std::uint8_t flags          = 0;
    const esp_err_t flag_result = readRegisters(FlagReg, &flags, 1);
    if (flag_result != ESP_OK) {
        ESP_LOGW(Tag, "RX8130 flag read failed: %s", esp_err_to_name(flag_result));
    }

    DateTime date_time;
    std::array<std::uint8_t, 7> raw_data = {};
    const esp_err_t time_result          = readDateTime(date_time, &raw_data);
    if (time_result != ESP_OK && time_result != ESP_ERR_INVALID_RESPONSE) {
        ESP_LOGW(Tag, "RX8130 time read failed: %s", esp_err_to_name(time_result));
        return ESP_OK;
    }
    if (time_result == ESP_ERR_INVALID_RESPONSE) {
        ESP_LOGW(Tag,
                 "RX8130 invalid registers [sec min hour week day month year]: %02X %02X %02X %02X %02X %02X "
                 "%02X",
                 static_cast<unsigned>(raw_data[0]), static_cast<unsigned>(raw_data[1]),
                 static_cast<unsigned>(raw_data[2]), static_cast<unsigned>(raw_data[3]),
                 static_cast<unsigned>(raw_data[4]), static_cast<unsigned>(raw_data[5]),
                 static_cast<unsigned>(raw_data[6]));
    }

    const bool voltage_low = flag_result == ESP_OK && (flags & VoltageLowFlag) != 0;
    if (voltage_low) {
        ESP_LOGW(Tag, "RX8130 voltage-low flag is set: 0x%02X", static_cast<unsigned>(flags));
    }
    if (time_result == ESP_OK && !voltage_low) {
        ESP_LOGI(Tag, "RX8130 initialized: %04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(date_time.year),
                 static_cast<unsigned>(date_time.month), static_cast<unsigned>(date_time.day),
                 static_cast<unsigned>(date_time.hour), static_cast<unsigned>(date_time.minute),
                 static_cast<unsigned>(date_time.second));
        return ESP_OK;
    }

    DateTime build_time;
    if (!getFirmwareBuildTime(build_time)) {
        ESP_LOGE(Tag, "failed to parse firmware build time");
        return ESP_OK;
    }
    if (!GetHAL().setDateTime(build_time)) {
        ESP_LOGE(Tag, "failed to initialize RX8130 from firmware build time");
        return ESP_OK;
    }
    const esp_err_t verify_result = readDateTime(date_time);
    if (verify_result != ESP_OK) {
        ESP_LOGE(Tag, "RX8130 initialization verification failed: %s", esp_err_to_name(verify_result));
        return ESP_OK;
    }

    if (voltage_low) {
        std::uint8_t current_flags = 0;
        esp_err_t clear_result     = readRegisters(FlagReg, &current_flags, 1);
        current_flags &= static_cast<std::uint8_t>(~VoltageLowFlag);
        if (clear_result == ESP_OK) {
            clear_result = writeRegisters(FlagReg, &current_flags, 1);
        }
        std::uint8_t verified_flags = 0;
        const esp_err_t flag_verify_result =
            clear_result == ESP_OK ? readRegisters(FlagReg, &verified_flags, 1) : clear_result;
        if (flag_verify_result != ESP_OK) {
            ESP_LOGW(Tag, "RX8130 voltage-low flag clear failed: %s", esp_err_to_name(flag_verify_result));
        } else if ((verified_flags & VoltageLowFlag) != 0) {
            ESP_LOGW(Tag, "RX8130 voltage-low flag remains set: 0x%02X", static_cast<unsigned>(verified_flags));
        }
    }

    ESP_LOGI(Tag, "RX8130 initialized from firmware build time: %04u-%02u-%02u %02u:%02u:%02u",
             static_cast<unsigned>(date_time.year), static_cast<unsigned>(date_time.month),
             static_cast<unsigned>(date_time.day), static_cast<unsigned>(date_time.hour),
             static_cast<unsigned>(date_time.minute), static_cast<unsigned>(date_time.second));
    return ESP_OK;
}

}  // namespace hal::internal

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
    return readDateTime(dateTime) == ESP_OK;
}

bool Hal::setDateTime(const DateTime& dateTime)
{
    if (rtc_device == nullptr || !dateTime.isValid()) {
        return false;
    }

    std::tm local_time          = {};
    local_time.tm_year          = dateTime.year - 1900;
    local_time.tm_mon           = dateTime.month - 1;
    local_time.tm_mday          = dateTime.day;
    local_time.tm_hour          = dateTime.hour;
    local_time.tm_min           = dateTime.minute;
    local_time.tm_sec           = dateTime.second;
    local_time.tm_isdst         = -1;
    const std::time_t timestamp = std::mktime(&local_time);
    std::tm utc_time            = {};
    if (timestamp < 0 || gmtime_r(&timestamp, &utc_time) == nullptr) {
        return false;
    }

    const std::array<std::uint8_t, 7> data = {
        decimalToBcd(static_cast<std::uint8_t>(utc_time.tm_sec)),
        decimalToBcd(static_cast<std::uint8_t>(utc_time.tm_min)),
        decimalToBcd(static_cast<std::uint8_t>(utc_time.tm_hour)),
        static_cast<std::uint8_t>(1U << utc_time.tm_wday),
        decimalToBcd(static_cast<std::uint8_t>(utc_time.tm_mday)),
        decimalToBcd(static_cast<std::uint8_t>(utc_time.tm_mon + 1)),
        decimalToBcd(static_cast<std::uint8_t>((utc_time.tm_year + 1900) % 100)),
    };

    std::uint8_t control = 0;
    if (readRegisters(ControlReg, &control, 1) != ESP_OK) {
        return false;
    }
    const std::uint8_t stopped_control = control | StopBit;
    if (writeRegisters(ControlReg, &stopped_control, 1) != ESP_OK) {
        return false;
    }
    const bool written = writeRegisters(TimeReg, data.data(), data.size()) == ESP_OK;
    control &= static_cast<std::uint8_t>(~StopBit);
    const bool restarted = writeRegisters(ControlReg, &control, 1) == ESP_OK;
    return written && restarted;
}
