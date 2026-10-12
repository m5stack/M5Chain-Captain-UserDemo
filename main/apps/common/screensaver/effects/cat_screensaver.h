/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../screensaver.h"
#include "cat_garden.h"

#include <cstdint>

namespace screensaver {

class CatScreensaver final : public Screensaver {
public:
    explicit CatScreensaver(CatGarden::Season season = CatGarden::Season::Autumn) : _garden(season)
    {
    }
    void init(int width, int height, std::uint32_t now) override;
    bool update(std::uint32_t now) override;
    void draw(lgfx::LGFX_Sprite& canvas) const override;

private:
    enum class State : std::uint8_t { Walk, Run, Idle, Clean, Paw, Sleep, Jump };

    void transition();
    void chooseDestination();
    void startJump();
    void drawShadow(lgfx::LGFX_Sprite& canvas) const;

    CatGarden _garden;

    State _state                  = State::Walk;
    bool _facing_right            = true;
    int _width                    = 0;
    int _height                   = 0;
    int _draw_x                   = 0;
    int _draw_y                   = 0;
    float _x                      = 0.0F;
    float _y                      = 0.0F;
    float _target_x               = 0.0F;
    float _target_y               = 0.0F;
    float _speed                  = 0.0F;
    int _frame                    = 0;
    std::uint32_t _last_tick      = 0;
    std::uint32_t _frame_timer    = 0;
    std::uint32_t _state_timer    = 0;
    std::uint32_t _state_duration = 0;
};

}  // namespace screensaver
