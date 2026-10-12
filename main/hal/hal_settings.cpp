/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <string>

namespace {

constexpr char Tag[]             = "HAL-SETTINGS";
constexpr char SettingsNs[]      = "device";
constexpr char SchemaKey[]       = "schema";
constexpr char BrightnessKey[]   = "brightness";
constexpr char RotationKey[]     = "rotation";
constexpr char ScreensaverKey[]  = "screensaver";
constexpr char VolumeKey[]       = "volume";
constexpr char ButtonSfxKey[]    = "button_sfx";
constexpr char TimezoneKey[]     = "timezone";
constexpr char Clock24HourKey[]  = "clock_24h";
constexpr char AutoTimeSyncKey[] = "auto_sync";
constexpr char EzDataUploadKey[] = "ezdata_upload";
constexpr std::int32_t SchemaVer = 5;

constexpr int MinimumBrightness          = 10;
constexpr int MaximumBrightness          = 100;
constexpr int MinimumVolume              = 0;
constexpr int MaximumVolume              = 100;
constexpr std::size_t MaximumTimezone    = 63;
constexpr std::size_t MaximumScreensaver = 31;

DeviceSettings sanitizeSettings(DeviceSettings settings)
{
    settings.backlightBrightness = std::clamp(settings.backlightBrightness, MinimumBrightness, MaximumBrightness);
    settings.displayRotation     = std::clamp(settings.displayRotation, 0, 3);
    if (settings.screensaver.empty() || settings.screensaver.size() > MaximumScreensaver) {
        settings.screensaver = DeviceSettings{}.screensaver;
    }
    settings.speakerVolume = std::clamp(settings.speakerVolume, MinimumVolume, MaximumVolume);
    if (settings.timezone.empty() || settings.timezone.size() > MaximumTimezone) {
        settings.timezone = DeviceSettings{}.timezone;
    }
    return settings;
}

bool settingsEqual(const DeviceSettings& left, const DeviceSettings& right)
{
    return left.backlightBrightness == right.backlightBrightness && left.displayRotation == right.displayRotation &&
           left.screensaver == right.screensaver && left.speakerVolume == right.speakerVolume &&
           left.buttonSfxEnabled == right.buttonSfxEnabled && left.timezone == right.timezone &&
           left.use24HourClock == right.use24HourClock && left.autoTimeSync == right.autoTimeSync &&
           left.ezDataUploadEnabled == right.ezDataUploadEnabled;
}

int readBoundedInt(nvs_handle_t handle, const char* key, int defaultValue, int minimum, int maximum)
{
    std::int32_t value     = 0;
    const esp_err_t result = nvs_get_i32(handle, key, &value);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return defaultValue;
    }
    if (result != ESP_OK) {
        ESP_LOGW(Tag, "failed to read %s: %s", key, esp_err_to_name(result));
        return defaultValue;
    }
    if (value < minimum || value > maximum) {
        ESP_LOGW(Tag, "invalid %s value: %" PRId32, key, value);
        return defaultValue;
    }
    return static_cast<int>(value);
}

bool readBool(nvs_handle_t handle, const char* key, bool defaultValue)
{
    std::uint8_t value     = 0;
    const esp_err_t result = nvs_get_u8(handle, key, &value);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return defaultValue;
    }
    if (result != ESP_OK) {
        ESP_LOGW(Tag, "failed to read %s: %s", key, esp_err_to_name(result));
        return defaultValue;
    }
    if (value > 1) {
        ESP_LOGW(Tag, "invalid %s value: %u", key, value);
        return defaultValue;
    }
    return value != 0;
}

std::string readString(nvs_handle_t handle, const char* key, const std::string& defaultValue, std::size_t maximumLength)
{
    std::size_t length     = 0;
    const esp_err_t result = nvs_get_str(handle, key, nullptr, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return defaultValue;
    }
    if (result != ESP_OK) {
        ESP_LOGW(Tag, "failed to query %s: %s", key, esp_err_to_name(result));
        return defaultValue;
    }
    if (length <= 1 || length > maximumLength + 1) {
        ESP_LOGW(Tag, "invalid %s length: %u", key, static_cast<unsigned>(length));
        return defaultValue;
    }

    std::string value(length, '\0');
    std::size_t read_length     = length;
    const esp_err_t read_result = nvs_get_str(handle, key, value.data(), &read_length);
    if (read_result != ESP_OK || read_length != length) {
        ESP_LOGW(Tag, "failed to read %s: %s", key, esp_err_to_name(read_result));
        return defaultValue;
    }
    value.resize(length - 1);
    return value;
}

esp_err_t initializeNvs()
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(Tag, "NVS requires recovery: %s", esp_err_to_name(result));
        result = nvs_flash_erase();
        if (result == ESP_OK) {
            result = nvs_flash_init();
        }
    }
    return result;
}

}  // namespace

bool Hal::loadDeviceSettings()
{
    DeviceSettings loaded;
    _device_settings_writable   = true;
    const esp_err_t init_result = initializeNvs();
    if (init_result != ESP_OK) {
        ESP_LOGE(Tag, "failed to initialize NVS: %s", esp_err_to_name(init_result));
        _device_settings      = loaded;
        _backlight_brightness = loaded.backlightBrightness;
        _display_rotation     = loaded.displayRotation;
        _speaker_volume       = loaded.speakerVolume;
        return false;
    }

    nvs_handle_t handle         = 0;
    const esp_err_t open_result = nvs_open(SettingsNs, NVS_READONLY, &handle);
    if (open_result == ESP_ERR_NVS_NOT_FOUND) {
        _device_settings      = loaded;
        _backlight_brightness = loaded.backlightBrightness;
        _display_rotation     = loaded.displayRotation;
        _speaker_volume       = loaded.speakerVolume;
        ESP_LOGI(Tag, "settings not found, using defaults");
        return true;
    }
    if (open_result != ESP_OK) {
        ESP_LOGE(Tag, "failed to open settings: %s", esp_err_to_name(open_result));
        _device_settings      = loaded;
        _backlight_brightness = loaded.backlightBrightness;
        _display_rotation     = loaded.displayRotation;
        _speaker_volume       = loaded.speakerVolume;
        return false;
    }

    std::int32_t schema           = 0;
    const esp_err_t schema_result = nvs_get_i32(handle, SchemaKey, &schema);
    if (schema_result != ESP_OK && schema_result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(Tag, "failed to read schema: %s", esp_err_to_name(schema_result));
    } else if (schema > SchemaVer) {
        ESP_LOGW(Tag, "unsupported settings schema: %" PRId32, schema);
        nvs_close(handle);
        _device_settings_writable = false;
        _device_settings          = loaded;
        _backlight_brightness     = loaded.backlightBrightness;
        _display_rotation         = loaded.displayRotation;
        _speaker_volume           = loaded.speakerVolume;
        return true;
    }

    loaded.backlightBrightness =
        readBoundedInt(handle, BrightnessKey, loaded.backlightBrightness, MinimumBrightness, MaximumBrightness);
    loaded.displayRotation     = readBoundedInt(handle, RotationKey, loaded.displayRotation, 0, 3);
    loaded.screensaver         = readString(handle, ScreensaverKey, loaded.screensaver, MaximumScreensaver);
    loaded.speakerVolume       = readBoundedInt(handle, VolumeKey, loaded.speakerVolume, MinimumVolume, MaximumVolume);
    loaded.buttonSfxEnabled    = readBool(handle, ButtonSfxKey, loaded.buttonSfxEnabled);
    loaded.timezone            = readString(handle, TimezoneKey, loaded.timezone, MaximumTimezone);
    loaded.use24HourClock      = readBool(handle, Clock24HourKey, loaded.use24HourClock);
    loaded.autoTimeSync        = readBool(handle, AutoTimeSyncKey, loaded.autoTimeSync);
    loaded.ezDataUploadEnabled = readBool(handle, EzDataUploadKey, loaded.ezDataUploadEnabled);
    nvs_close(handle);

    _device_settings      = sanitizeSettings(std::move(loaded));
    _backlight_brightness = _device_settings.backlightBrightness;
    _display_rotation     = _device_settings.displayRotation;
    _speaker_volume       = _device_settings.speakerVolume;
    ESP_LOGI(
        Tag,
        "settings loaded: brightness=%d rotation=%d screensaver=%s volume=%d button_sfx=%d timezone=%s clock_24h=%d "
        "auto_sync=%d ezdata_upload=%d",
        _device_settings.backlightBrightness, _device_settings.displayRotation, _device_settings.screensaver.c_str(),
        _device_settings.speakerVolume, _device_settings.buttonSfxEnabled, _device_settings.timezone.c_str(),
        _device_settings.use24HourClock, _device_settings.autoTimeSync, _device_settings.ezDataUploadEnabled);
    return true;
}

bool Hal::saveDeviceSettings(const DeviceSettings& settings)
{
    if (!_device_settings_writable) {
        ESP_LOGE(Tag, "refusing to overwrite a newer settings schema");
        return false;
    }

    DeviceSettings sanitized = sanitizeSettings(settings);
    if (settingsEqual(sanitized, _device_settings)) {
        return true;
    }

    nvs_handle_t handle         = 0;
    const esp_err_t open_result = nvs_open(SettingsNs, NVS_READWRITE, &handle);
    if (open_result != ESP_OK) {
        ESP_LOGE(Tag, "failed to open settings for writing: %s", esp_err_to_name(open_result));
        return false;
    }

    esp_err_t result = nvs_set_i32(handle, SchemaKey, SchemaVer);
    if (result == ESP_OK) {
        result = nvs_set_i32(handle, BrightnessKey, sanitized.backlightBrightness);
    }
    if (result == ESP_OK) {
        result = nvs_set_i32(handle, RotationKey, sanitized.displayRotation);
    }
    if (result == ESP_OK) {
        result = nvs_set_str(handle, ScreensaverKey, sanitized.screensaver.c_str());
    }
    if (result == ESP_OK) {
        result = nvs_set_i32(handle, VolumeKey, sanitized.speakerVolume);
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, ButtonSfxKey, sanitized.buttonSfxEnabled ? 1 : 0);
    }
    if (result == ESP_OK) {
        result = nvs_set_str(handle, TimezoneKey, sanitized.timezone.c_str());
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, Clock24HourKey, sanitized.use24HourClock ? 1 : 0);
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, AutoTimeSyncKey, sanitized.autoTimeSync ? 1 : 0);
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, EzDataUploadKey, sanitized.ezDataUploadEnabled ? 1 : 0);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);

    if (result != ESP_OK) {
        ESP_LOGE(Tag, "failed to save settings: %s", esp_err_to_name(result));
        return false;
    }

    _device_settings = std::move(sanitized);
    ESP_LOGI(Tag, "settings saved");
    return true;
}
