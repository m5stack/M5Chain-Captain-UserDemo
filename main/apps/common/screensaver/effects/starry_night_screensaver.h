/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../screensaver.h"

namespace screensaver {

class StarryNightScreensaver final : public Screensaver {
public:
    void init(int width, int height, std::uint32_t now) override;
    bool update(std::uint32_t now) override;
    void draw(lgfx::LGFX_Sprite& canvas) const override;

private:
    struct Light {
        std::int16_t x;
        std::int16_t y;
        std::uint16_t revealAt;
        std::uint8_t width;
        std::uint8_t height;
        std::uint8_t nominal;
        std::uint8_t target;
        std::uint8_t level = 0;
    };

    struct Meteor {
        float x                = 0;
        float y                = 0;
        float velocityX        = 0;
        float velocityY        = 0;
        std::uint32_t age      = 0;
        std::uint32_t duration = 0;
        bool active            = false;
    };

    std::vector<std::int16_t> _skyline;
    Meteor _meteor;
    std::uint32_t _meteor_wait  = 0;
    std::uint32_t _beacon_phase = 0;
    int _beacon_x               = 0;
    int _beacon_y               = 0;
    bool _beacon_on             = false;
    std::vector<Light> _stars;
    std::vector<Light> _windows;
    std::uint32_t _last_frame  = 0;
    std::uint32_t _last_change = 0;
    std::uint32_t _reveal_age  = 0;

    bool updateMeteor(std::uint32_t elapsed);
    bool updateLights(std::vector<Light>& lights, int step);
};

}  // namespace screensaver
