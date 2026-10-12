/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <M5IOE1.h>
#include <M5PM1.h>
#include <algorithm>
#include <driver/gpio.h>
#include <esp_check.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

constexpr char Tag[]                   = "HAL-PWR";
constexpr gpio_num_t I2cScl            = GPIO_NUM_2;
constexpr gpio_num_t I2cSda            = GPIO_NUM_3;
constexpr std::uint8_t IoeAddress      = 0x4f;
constexpr std::uint8_t ChargeStatusPin = M5IOE1_PIN_3;
constexpr std::uint8_t BoostControlPin = M5IOE1_PIN_4;
constexpr std::uint8_t IrPowerPin      = M5IOE1_PIN_2;
constexpr std::uint8_t GrovePowerPin   = M5PM1_GPIO_NUM_0;
constexpr std::uint8_t ChainPowerPin   = M5PM1_GPIO_NUM_3;
constexpr std::uint16_t BatteryEmptyMv = 3300;
constexpr std::uint16_t BatteryFullMv  = 4200;
// M5IOE1 exposes one PWM frequency for the LCD backlight and RGB channels.
constexpr std::uint16_t SharedPwmRateHz = 20000;
constexpr const char* IoeLogTags[]      = {
    "M5IOE1",     "M5IOE1_I2C", "M5IOE1_GPIO", "M5IOE1_ADC", "M5IOE1_PWM",
    "M5IOE1_LED", "M5IOE1_AMP", "M5IOE1_IRQ",  "M5IOE1_SYS",
};

i2c_master_bus_handle_t i2c_bus = nullptr;
M5PM1 pmic_device;
M5IOE1 ioe_device;
bool pmic_ready = false;
bool ioe_ready  = false;

bool setIoeOutput(std::uint8_t pin, bool enabled, bool verify)
{
    m5ioe1_err_t error = M5IOE1_OK;
    ioe_device.pinModeWithRes(pin, OUTPUT, &error);
    if (error != M5IOE1_OK || ioe_device.setDriveMode(pin, M5IOE1_DRIVE_PUSHPULL) != M5IOE1_OK) {
        return false;
    }
    ioe_device.digitalWriteWithRes(pin, enabled ? HIGH : LOW, &error);
    if (error != M5IOE1_OK) {
        return false;
    }
    return !verify || (ioe_device.digitalReadWithRes(pin, &error) == (enabled ? HIGH : LOW) && error == M5IOE1_OK);
}

bool setPmicOutput(std::uint8_t pin, bool enabled)
{
    m5pm1_err_t error = M5PM1_OK;
    pmic_device.pinModeWithRes(pin, OUTPUT, &error);
    if (error != M5PM1_OK) {
        return false;
    }
    pmic_device.digitalWriteWithRes(pin, enabled ? HIGH : LOW, &error);
    return error == M5PM1_OK;
}

}  // namespace

namespace hal::internal {

esp_err_t powerInit()
{
    for (const char* tag : IoeLogTags) {
        esp_log_level_set(tag, ESP_LOG_NONE);
    }

    i2c_master_bus_config_t config      = {};
    config.clk_source                   = I2C_CLK_SRC_DEFAULT;
    config.i2c_port                     = I2C_NUM_0;
    config.scl_io_num                   = I2cScl;
    config.sda_io_num                   = I2cSda;
    config.glitch_ignore_cnt            = 7;
    config.flags.enable_internal_pullup = false;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &i2c_bus), Tag, "failed to initialize I2C bus");

    if (pmic_device.begin(i2c_bus, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K) != M5PM1_OK) {
        ESP_LOGE(Tag, "M5PM1 initialization failed");
        return ESP_FAIL;
    }
    pmic_ready = true;
    if (pmic_device.setI2cSleepTime(0) != M5PM1_OK || pmic_device.setChargeEnable(true) != M5PM1_OK ||
        pmic_device.setLedEnLevel(true) != M5PM1_OK || pmic_device.setBoostEnable(false) != M5PM1_OK ||
        pmic_device.setSingleResetDisable(true) != M5PM1_OK || pmic_device.setDoubleOffDisable(false) != M5PM1_OK) {
        ESP_LOGE(Tag, "M5PM1 startup configuration failed");
        return ESP_FAIL;
    }

    if (ioe_device.begin(i2c_bus, IoeAddress, M5IOE1_I2C_FREQ_100K, M5IOE1_INT_MODE_POLLING) != M5IOE1_OK) {
        ESP_LOGE(Tag, "M5IOE1 initialization failed");
        return ESP_FAIL;
    }
    ioe_ready = true;
    if (ioe_device.setI2cSleepTime(0) != M5IOE1_OK || ioe_device.setPwmFrequency(SharedPwmRateHz) != M5IOE1_OK) {
        ESP_LOGE(Tag, "M5IOE1 startup configuration failed");
        return ESP_FAIL;
    }

    m5ioe1_err_t charge_error = M5IOE1_OK;
    ioe_device.pinModeWithRes(ChargeStatusPin, INPUT_PULLUP, &charge_error);
    if (charge_error != M5IOE1_OK || !setIoeOutput(BoostControlPin, false, false) ||
        !setPmicOutput(GrovePowerPin, false) || !setPmicOutput(ChainPowerPin, false) ||
        !setIoeOutput(IrPowerPin, false, true)) {
        ESP_LOGE(Tag, "power rail initialization failed");
        return ESP_FAIL;
    }

    ESP_LOGI(Tag, "power initialized, external rails and IR are off");
    return ESP_OK;
}

i2c_master_bus_handle_t i2cBus()
{
    return i2c_bus;
}

M5PM1* pmic()
{
    return pmic_ready ? &pmic_device : nullptr;
}

M5IOE1* ioe()
{
    return ioe_ready ? &ioe_device : nullptr;
}

}  // namespace hal::internal

std::uint16_t Hal::getBatteryVoltage()
{
    std::uint16_t millivolts = 0;
    auto* device             = hal::internal::pmic();
    return device != nullptr && device->readVbat(&millivolts) == M5PM1_OK ? millivolts : 0;
}

int Hal::getBatteryLevel()
{
    const std::uint16_t millivolts = getBatteryVoltage();
    if (millivolts <= BatteryEmptyMv) {
        return 0;
    }
    if (millivolts >= BatteryFullMv) {
        return 100;
    }
    return static_cast<int>((millivolts - BatteryEmptyMv) * 100U / (BatteryFullMv - BatteryEmptyMv));
}

PowerSource Hal::getPowerSource()
{
    auto* device           = hal::internal::pmic();
    m5pm1_pwr_src_t source = M5PM1_PWR_SRC_UNKNOWN;
    if (device == nullptr || device->getPowerSource(&source) != M5PM1_OK) {
        return PowerSource::Unknown;
    }
    switch (source) {
        case M5PM1_PWR_SRC_BAT:
            return PowerSource::Battery;
        case M5PM1_PWR_SRC_5VIN:
            return PowerSource::TypeC;
        case M5PM1_PWR_SRC_5VINOUT:
            return PowerSource::Chain;
        default:
            return PowerSource::Unknown;
    }
}

std::uint16_t Hal::getInputVoltage()
{
    auto* device = hal::internal::pmic();
    if (device == nullptr) {
        return 0;
    }

    std::uint16_t millivolts = 0;
    if (getPowerSource() == PowerSource::Chain) {
        return device->read5VInOut(&millivolts) == M5PM1_OK ? millivolts : 0;
    }
    return device->readVin(&millivolts) == M5PM1_OK ? millivolts : 0;
}

bool Hal::isBatteryCharging()
{
    auto* device = hal::internal::ioe();
    if (device == nullptr) {
        return false;
    }
    m5ioe1_err_t error = M5IOE1_OK;
    const int level    = device->digitalReadWithRes(ChargeStatusPin, &error);
    return error == M5IOE1_OK && level == LOW;
}

bool Hal::setBatteryChargingEnabled(bool enabled)
{
    auto* device = hal::internal::pmic();
    if (device == nullptr || device->setChargeEnable(enabled) != M5PM1_OK) {
        return false;
    }
    _battery_charging_enabled = enabled;
    return true;
}

bool Hal::isBatteryChargingEnabled()
{
    auto* device        = hal::internal::pmic();
    std::uint8_t config = 0;
    return device != nullptr && device->getPowerConfig(&config) == M5PM1_OK && (config & M5PM1_PWR_CFG_CHG_EN) != 0;
}

bool Hal::setPortPowerEnabled(bool enabled)
{
    auto* power = hal::internal::pmic();
    auto* ioe   = hal::internal::ioe();
    if (power == nullptr || ioe == nullptr || !setIoeOutput(BoostControlPin, enabled, false) ||
        !setPmicOutput(GrovePowerPin, enabled) || !setPmicOutput(ChainPowerPin, enabled) ||
        power->setBoostEnable(enabled) != M5PM1_OK) {
        return false;
    }
    _port_power_enabled = enabled;
    return true;
}

bool Hal::setPowerButtonDoubleClickEnabled(bool enabled)
{
    if (_power_button_double_click_enabled == enabled) {
        return true;
    }

    auto* device = hal::internal::pmic();
    if (device == nullptr || device->setDoubleOffDisable(!enabled) != M5PM1_OK) {
        return false;
    }
    _power_button_double_click_enabled = enabled;
    return true;
}

bool Hal::setIrPowerEnabled(bool enabled)
{
    if (hal::internal::ioe() == nullptr || !setIoeOutput(IrPowerPin, enabled, true)) {
        return false;
    }
    _ir_power_enabled = enabled;
    if (enabled) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
