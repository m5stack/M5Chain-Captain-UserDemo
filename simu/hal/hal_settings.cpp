/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>
#include <apps/common/screensaver/screensaver.h>
#include <utility>

namespace {

constexpr int MinimumBrightness       = 10;
constexpr int MaximumBrightness       = 100;
constexpr int MinimumVolume           = 0;
constexpr int MaximumVolume           = 100;
constexpr std::size_t MaximumTimezone = 63;

DeviceSettings sanitizeSettings(DeviceSettings settings)
{
    settings.backlightBrightness = std::clamp(settings.backlightBrightness, MinimumBrightness, MaximumBrightness);
    settings.displayRotation     = std::clamp(settings.displayRotation, 0, 3);
    settings.screensaver         = screensaver::find(settings.screensaver).id;
    settings.speakerVolume       = std::clamp(settings.speakerVolume, MinimumVolume, MaximumVolume);
    if (settings.timezone.empty() || settings.timezone.size() > MaximumTimezone) {
        settings.timezone = DeviceSettings{}.timezone;
    }
    return settings;
}

}  // namespace

bool Hal::loadDeviceSettings()
{
    _device_settings      = sanitizeSettings(DeviceSettings{});
    _backlight_brightness = _device_settings.backlightBrightness;
    _display_rotation     = _device_settings.displayRotation;
    _speaker_volume       = _device_settings.speakerVolume;
    return true;
}

bool Hal::saveDeviceSettings(const DeviceSettings& settings)
{
    _device_settings = sanitizeSettings(settings);
    return true;
}
