/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstdint>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

#include <hal/hal.h>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace view {

struct StatusBarPalette {
    std::uint32_t background = 0xCBDEEE;
    std::uint32_t foreground = 0x63717D;
    std::uint32_t error      = 0xF95757;
    std::uint32_t charging   = 0x1AFF66;
    std::uint32_t lowBattery = 0xFF5656;
};

class StatusBar {
public:
    ~StatusBar();

    void create();
    void destroy();
    bool update();
    void setWifiSignal(WifiSignal signal);
    void draw(lgfx::LGFX_Sprite& canvas, const StatusBarPalette& palette = {}) const;

private:
    enum class VolumeLevel : std::uint8_t {
        Muted,
        Low,
        High,
    };

    std::array<char, 9> _time_text = {'-', '-', ':', '-', '-', '\0', '\0', '\0', '\0'};
    uitk::AnimateValue _charging_transition;
    float _charging_transition_value = 0.0F;
    std::uint32_t _last_tick         = 0;
    int _battery_level               = 0;
    VolumeLevel _volume_level        = VolumeLevel::Muted;
    bool _battery_charging           = false;
    bool _battery_level_valid        = false;
    bool _volume_level_valid         = false;
    WifiSignal _wifi_signal          = WifiSignal::Off;
    bool _active                     = false;
    bool _dirty                      = false;

    void configureChargingAnimation();
    bool updateBatteryLevel();
    bool updateVolumeLevel();
    void updateChargingState(bool charging);
    bool updateTime();
};

}  // namespace view
