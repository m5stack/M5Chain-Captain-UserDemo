/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <array>
#include <bmi270.h>
#include <cstring>
#include <esp_log.h>
#include <esp_rom_sys.h>

namespace {

constexpr char Tag[]              = "HAL-IMU";
constexpr std::uint8_t ImuAddress = 0x68;
constexpr int I2cTimeoutMs        = 100;
constexpr float AccelScale        = 8192.0f;
constexpr float GyroScale         = 32.768f;

i2c_master_dev_handle_t imu_device = nullptr;
bmi2_dev imu                       = {};
bool imu_ready                     = false;

BMI2_INTF_RETURN_TYPE imuRead(std::uint8_t reg, std::uint8_t* data, std::uint32_t size, void*)
{
    if (data == nullptr || size == 0 || size > 32) {
        return BMI2_E_INVALID_INPUT;
    }
    return i2c_master_transmit_receive(imu_device, &reg, 1, data, size, I2cTimeoutMs) == ESP_OK ? BMI2_OK
                                                                                                : BMI2_E_COM_FAIL;
}

BMI2_INTF_RETURN_TYPE imuWrite(std::uint8_t reg, const std::uint8_t* data, std::uint32_t size, void*)
{
    if (data == nullptr || size == 0 || size > 32) {
        return BMI2_E_INVALID_INPUT;
    }
    std::array<std::uint8_t, 33> buffer = {};
    buffer[0]                           = reg;
    std::memcpy(buffer.data() + 1, data, size);
    return i2c_master_transmit(imu_device, buffer.data(), size + 1, I2cTimeoutMs) == ESP_OK ? BMI2_OK : BMI2_E_COM_FAIL;
}

void imuDelay(std::uint32_t period, void*)
{
    esp_rom_delay_us(period);
}

}  // namespace

namespace hal::internal {

esp_err_t imuInit()
{
    if (i2cBus() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_device_config_t device_config = {};
    device_config.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    device_config.device_address      = ImuAddress;
    device_config.scl_speed_hz        = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2cBus(), &device_config, &imu_device), Tag,
                        "failed to attach BMI270");

    imu.intf            = BMI2_I2C_INTF;
    imu.read            = imuRead;
    imu.write           = imuWrite;
    imu.delay_us        = imuDelay;
    imu.intf_ptr        = &imu_device;
    imu.read_write_len  = 30;
    imu.config_file_ptr = nullptr;
    int8_t result       = bmi270_init(&imu);
    if (result != BMI2_OK) {
        ESP_LOGE(Tag, "BMI270 initialization failed: %d", result);
        return ESP_FAIL;
    }

    std::array<bmi2_sens_config, 2> sensor_config = {};
    sensor_config[0].type                         = BMI2_ACCEL;
    sensor_config[1].type                         = BMI2_GYRO;
    result = bmi2_get_sensor_config(sensor_config.data(), sensor_config.size(), &imu);
    if (result != BMI2_OK) {
        ESP_LOGE(Tag, "failed to read BMI270 configuration: %d", result);
        return ESP_FAIL;
    }
    sensor_config[0].cfg.acc.odr         = BMI2_ACC_ODR_200HZ;
    sensor_config[0].cfg.acc.range       = BMI2_ACC_RANGE_4G;
    sensor_config[0].cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    sensor_config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    sensor_config[1].cfg.gyr.odr         = BMI2_GYR_ODR_200HZ;
    sensor_config[1].cfg.gyr.range       = BMI2_GYR_RANGE_1000;
    sensor_config[1].cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    sensor_config[1].cfg.gyr.noise_perf  = BMI2_POWER_OPT_MODE;
    sensor_config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    result                               = bmi2_set_sensor_config(sensor_config.data(), sensor_config.size(), &imu);
    if (result != BMI2_OK) {
        ESP_LOGE(Tag, "failed to configure BMI270: %d", result);
        return ESP_FAIL;
    }

    const std::array<std::uint8_t, 2> sensors = {BMI2_ACCEL, BMI2_GYRO};
    result                                    = bmi270_sensor_enable(sensors.data(), sensors.size(), &imu);
    if (result != BMI2_OK) {
        ESP_LOGE(Tag, "failed to enable BMI270 sensors: %d", result);
        return ESP_FAIL;
    }
    imu_ready = true;
    ESP_LOGI(Tag, "BMI270 initialized");
    return ESP_OK;
}

}  // namespace hal::internal

bool Hal::updateImuData()
{
    if (!imu_ready) {
        return false;
    }
    bmi2_sens_data data = {};
    if (bmi2_get_sensor_data(&data, &imu) != BMI2_OK) {
        return false;
    }

    // Match the simulator's screen-space support-force convention.
    _imu_data.accelX = -data.acc.y / AccelScale;
    _imu_data.accelY = data.acc.x / AccelScale;
    _imu_data.accelZ = -data.acc.z / AccelScale;
    _imu_data.gyroX  = -data.gyr.y / GyroScale;
    _imu_data.gyroY  = data.gyr.x / GyroScale;
    _imu_data.gyroZ  = -data.gyr.z / GyroScale;
    return true;
}
