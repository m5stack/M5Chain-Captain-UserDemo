/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "status_bar.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <cstdio>
#include <hal/hal.h>

namespace {

constexpr int ScreenWidth                = 240;
constexpr int StatusHeight               = 24;
constexpr int VolumeOriginX              = 5;
constexpr int VolumeOriginY              = 2;
constexpr int WifiOriginX                = 177;
constexpr int WifiOriginY                = 3;
constexpr std::uint32_t UpdateIntervalMs = 1000;
constexpr bool WifiVisible               = false;

constexpr int BatteryOriginX          = 211;
constexpr int BatteryChargingOffset   = 12;
constexpr int BatteryFillOffsetX      = 2;
constexpr int BatteryFillOffsetY      = 7;
constexpr int BatteryFillMaxWidth     = 17;
constexpr int BatteryFillHeight       = 10;
constexpr int BatteryFillRadius       = 1;
constexpr int BatteryLightningOffsetY = 6;
constexpr int BatteryLowThreshold     = 20;

constexpr int BatteryLightningX = BatteryOriginX + BatteryChargingOffset;

constexpr float ChargingVisualDuration = 0.42F;
constexpr float ChargingBounce         = 0.12F;
constexpr float ChargingBodyPhaseEnd   = 0.82F;
constexpr float ChargingLightningStart = 0.70F;
constexpr float AnimationEpsilon       = 0.0005F;

const lgfx::LVGLFont StatusFont{&font_noto_sans_semibold_14};

void drawImage(lgfx::LGFX_Sprite& canvas, int x, int y, const lv_image_dsc_t* image, std::uint32_t color,
               std::uint32_t background)
{
    if (image == nullptr || image->header.cf != LV_COLOR_FORMAT_A8) {
        return;
    }

    canvas.pushGrayscaleImage(x, y, image->header.w, image->header.h, image->data, lgfx::color_depth_t::grayscale_8bit,
                              color, background);
}

void drawA8Mask(lgfx::LGFX_Sprite& canvas, int x, int y, const lv_image_dsc_t* image, std::uint32_t color,
                std::uint8_t opacity = 255)
{
    if (image == nullptr || image->header.cf != LV_COLOR_FORMAT_A8 || image->data == nullptr || opacity == 0U) {
        return;
    }

    const int width  = static_cast<int>(image->header.w);
    const int height = static_cast<int>(image->header.h);
    const int stride = static_cast<int>(image->header.stride);
    if (width <= 0 || height <= 0 || stride < width) {
        return;
    }

    const auto* pixels = static_cast<const std::uint8_t*>(image->data);
    for (int row = 0; row < height; ++row) {
        int run_start          = -1;
        std::uint8_t run_alpha = 0;

        const auto flush_run = [&](int run_end) {
            if (run_start < 0 || run_end <= run_start || run_alpha == 0U) {
                return;
            }

            const int run_width = run_end - run_start;
            if (run_alpha == 255U) {
                canvas.fillRect(x + run_start, y + row, run_width, 1, color);
            } else {
                canvas.fillRectAlpha(x + run_start, y + row, run_width, 1, run_alpha, color);
            }
        };

        for (int column = 0; column < width; ++column) {
            const std::uint8_t source_alpha = pixels[row * stride + column];
            const std::uint8_t alpha =
                opacity == 255U ? source_alpha : static_cast<std::uint8_t>((source_alpha * opacity + 127U) / 255U);
            if (alpha == 0U) {
                flush_run(column);
                run_start = -1;
                continue;
            }
            if (run_start >= 0 && alpha == run_alpha) {
                continue;
            }

            flush_run(column);
            run_start = column;
            run_alpha = alpha;
        }
        flush_run(width);
    }
}

int batteryFillWidth(int level)
{
    const int clamped_level = std::clamp(level, 0, 100);
    if (clamped_level == 0) {
        return 0;
    }

    // Round up so the reference levels (20%, 80%, and 100%) render as 4, 14,
    // and 17 pixels instead of disappearing one pixel early.
    return (BatteryFillMaxWidth * clamped_level + 99) / 100;
}

void drawBattery(lgfx::LGFX_Sprite& canvas, int level, float charging_transition, const view::StatusBarPalette& palette)
{
    const float transition      = std::clamp(charging_transition, 0.0F, 1.0F);
    const float body_transition = std::clamp(transition / ChargingBodyPhaseEnd, 0.0F, 1.0F);
    const int origin_x          = static_cast<int>(
        std::lround(static_cast<float>(BatteryOriginX) - static_cast<float>(BatteryChargingOffset) * body_transition));
    const int fill_width = batteryFillWidth(level);
    if (fill_width > 0) {
        const std::uint32_t normal_color = level <= BatteryLowThreshold ? palette.lowBattery : palette.foreground;
        const std::uint32_t fill_color   = uitk::color::blend_in_opacity(uitk::color::Rgb_t(normal_color),
                                                                         uitk::color::Rgb_t(palette.charging), transition)
                                             .toHex();
        canvas.fillRoundRect(origin_x + BatteryFillOffsetX, BatteryFillOffsetY, fill_width, BatteryFillHeight,
                             BatteryFillRadius, fill_color);
    }

    drawA8Mask(canvas, origin_x, 0, &image_status_battery_outline, palette.foreground);

    const float lightning_transition =
        std::clamp((transition - ChargingLightningStart) / (1.0F - ChargingLightningStart), 0.0F, 1.0F);
    const auto lightning_opacity = static_cast<std::uint8_t>(std::lround(lightning_transition * 255.0F));
    drawA8Mask(canvas, BatteryLightningX, BatteryLightningOffsetY, &image_status_battery_lightning, palette.foreground,
               lightning_opacity);
}

}  // namespace

namespace view {

StatusBar::~StatusBar()
{
    destroy();
}

void StatusBar::create()
{
    destroy();

    _active = true;
    configureChargingAnimation();
    updateTime();
    _battery_charging = GetHAL().isBatteryCharging();
    updateBatteryLevel();
    updateVolumeLevel();
    if constexpr (WifiVisible) {
        setWifiSignal(GetHAL().getWifiSignal());
    }
    _charging_transition.teleport(_battery_charging ? 1.0F : 0.0F);
    _charging_transition.update(static_cast<float>(GetHAL().millis()) / 1000.0F);
    _charging_transition_value = _charging_transition.directValue();
    _last_tick                 = GetHAL().millis();
}

void StatusBar::destroy()
{
    _time_text = {'-', '-', ':', '-', '-', '\0', '\0', '\0', '\0'};
    _charging_transition.teleport(0.0F);
    _charging_transition_value = 0.0F;
    _last_tick                 = 0;
    _battery_level             = 0;
    _volume_level              = VolumeLevel::Muted;
    _battery_charging          = false;
    _battery_level_valid       = false;
    _volume_level_valid        = false;
    _wifi_signal               = WifiSignal::Off;
    _active                    = false;
    _dirty                     = false;
}

bool StatusBar::update()
{
    if (!_active) {
        return false;
    }

    const std::uint32_t now = GetHAL().millis();
    if (now - _last_tick >= UpdateIntervalMs) {
        _last_tick = now;
        updateTime();
        updateBatteryLevel();
        updateVolumeLevel();
        updateChargingState(GetHAL().isBatteryCharging());
        if constexpr (WifiVisible) {
            setWifiSignal(GetHAL().getWifiSignal());
        }
    }

    const float previous_transition = _charging_transition_value;
    _charging_transition.update(static_cast<float>(now) / 1000.0F);
    const bool transition_done = _charging_transition.done();
    const float current_transition =
        transition_done ? (_battery_charging ? 1.0F : 0.0F) : _charging_transition.directValue();
    _charging_transition_value = current_transition;
    if (!transition_done || std::abs(current_transition - previous_transition) > AnimationEpsilon) {
        _dirty = true;
    }

    const bool changed = _dirty;
    _dirty             = false;
    return changed;
}

void StatusBar::configureChargingAnimation()
{
    auto& options              = _charging_transition.springOptions();
    options.duration           = 0.0F;
    options.visualDuration     = ChargingVisualDuration;
    options.bounce             = ChargingBounce;
    options.velocity           = 0.0F;
    options.restDelta          = 0.002F;
    options.restSpeed          = 0.02F;
    _charging_transition.delay = 0.0F;
}

bool StatusBar::updateBatteryLevel()
{
    const int next_level = std::clamp(GetHAL().getBatteryLevel(), 0, 100);
    bool changed         = false;

    if (!_battery_level_valid) {
        _battery_level       = next_level;
        _battery_level_valid = true;
        changed              = true;
    } else if (_battery_level != next_level) {
        _battery_level = next_level;
        changed        = true;
    }

    _dirty = _dirty || changed;
    return changed;
}

bool StatusBar::updateVolumeLevel()
{
    const int volume = GetHAL().getSpeakerVolume();
    const VolumeLevel next_level =
        volume <= 0 ? VolumeLevel::Muted : (volume <= 50 ? VolumeLevel::Low : VolumeLevel::High);
    if (_volume_level_valid && _volume_level == next_level) {
        return false;
    }

    _volume_level       = next_level;
    _volume_level_valid = true;
    _dirty              = true;
    return true;
}

void StatusBar::updateChargingState(bool charging)
{
    if (!_active || _battery_charging == charging) {
        return;
    }

    _battery_charging = charging;
    _charging_transition.move(charging ? 1.0F : 0.0F);
    _dirty = true;
}

void StatusBar::setWifiSignal(WifiSignal signal)
{
    if (_wifi_signal == signal) {
        return;
    }

    _wifi_signal = signal;
    _dirty       = true;
}

bool StatusBar::updateTime()
{
    if (!_active) {
        return false;
    }

    std::array<char, 9> next_text = {'-', '-', ':', '-', '-', '\0', '\0', '\0', '\0'};
    DateTime date_time;
    if (GetHAL().getDateTime(date_time)) {
        const unsigned hour   = date_time.hour % 24U;
        const unsigned minute = date_time.minute % 60U;
        if (GetHAL().deviceSettings().use24HourClock) {
            std::snprintf(next_text.data(), next_text.size(), "%02u:%02u", hour, minute);
        } else {
            const unsigned display_hour = hour % 12 == 0 ? 12 : hour % 12;
            std::snprintf(next_text.data(), next_text.size(), "%u:%02u %s", display_hour, minute,
                          hour < 12 ? "AM" : "PM");
        }
    }

    if (_time_text == next_text) {
        return false;
    }

    _time_text = next_text;
    _dirty     = true;
    return true;
}

void StatusBar::draw(lgfx::LGFX_Sprite& canvas, const StatusBarPalette& palette) const
{
    if (!_active) {
        return;
    }

    canvas.fillRect(0, 0, ScreenWidth, StatusHeight, palette.background);
    const lv_image_dsc_t* volume_image = &image_status_no_sound;
    if (_volume_level == VolumeLevel::Low) {
        volume_image = &image_status_volume_down;
    } else if (_volume_level == VolumeLevel::High) {
        volume_image = &image_status_volume_up;
    }
    drawImage(canvas, VolumeOriginX, VolumeOriginY, volume_image, palette.foreground, palette.background);

    if constexpr (WifiVisible) {
        const lv_image_dsc_t* wifi_image = nullptr;
        switch (_wifi_signal) {
            case WifiSignal::Low:
                wifi_image = &image_status_wifi_low;
                break;
            case WifiSignal::Medium:
                wifi_image = &image_status_wifi_medium;
                break;
            case WifiSignal::High:
                wifi_image = &image_status_wifi_high;
                break;
            case WifiSignal::Failed:
                wifi_image = &image_status_wifi_failed;
                break;
            default:
                break;
        }
        drawImage(canvas, WifiOriginX, WifiOriginY, wifi_image,
                  _wifi_signal == WifiSignal::Failed ? palette.error : palette.foreground, palette.background);
    }

    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::top_center);
    canvas.setTextColor(palette.foreground);
    canvas.setBaseColor(palette.background);
    canvas.setFont(&StatusFont);
    canvas.drawString(_time_text.data(), ScreenWidth / 2, 6);

    drawBattery(canvas, _battery_level, _charging_transition_value, palette);
}

}  // namespace view
