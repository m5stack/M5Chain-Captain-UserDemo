/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <M5IOE1.h>
#include <esp_log.h>

namespace {

constexpr char Tag[]                = "HAL-RGB";
constexpr std::uint8_t RedPin       = M5IOE1_PIN_10;
constexpr std::uint8_t GreenPin     = M5IOE1_PIN_8;
constexpr std::uint8_t BluePin      = M5IOE1_PIN_9;
constexpr std::uint8_t RedChannel   = M5IOE1_PWM_CH4;
constexpr std::uint8_t GreenChannel = M5IOE1_PWM_CH2;
constexpr std::uint8_t BlueChannel  = M5IOE1_PWM_CH1;

bool configureOutput(M5IOE1* device, std::uint8_t pin)
{
    m5ioe1_err_t error = M5IOE1_OK;
    device->pinModeWithRes(pin, OUTPUT, &error);
    if (error != M5IOE1_OK || device->setDriveMode(pin, M5IOE1_DRIVE_PUSHPULL) != M5IOE1_OK) {
        return false;
    }
    device->digitalWriteWithRes(pin, HIGH, &error);
    return error == M5IOE1_OK;
}

std::uint8_t toDuty(std::uint8_t value)
{
    return static_cast<std::uint8_t>((static_cast<std::uint16_t>(value) * 100U + 127U) / 255U);
}

bool setDuties(M5IOE1* device, std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    return device->setPwmDuty(RedChannel, toDuty(red)) == M5IOE1_OK &&
           device->setPwmDuty(GreenChannel, toDuty(green)) == M5IOE1_OK &&
           device->setPwmDuty(BlueChannel, toDuty(blue)) == M5IOE1_OK;
}

}  // namespace

namespace hal::internal {

esp_err_t rgbInit()
{
    auto* device = ioe();
    if (device == nullptr || !configureOutput(device, RedPin) || !configureOutput(device, GreenPin) ||
        !configureOutput(device, BluePin) || !setDuties(device, 0, 0, 0)) {
        ESP_LOGE(Tag, "RGB initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(Tag, "RGB initialized and off");
    return ESP_OK;
}

}  // namespace hal::internal

bool Hal::setRgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    auto* device = hal::internal::ioe();
    if (device == nullptr || !setDuties(device, red, green, blue)) {
        return false;
    }
    _rgb_color = {.red = red, .green = green, .blue = blue};
    return true;
}
