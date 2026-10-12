/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "../screensaver.h"
#include <array>

namespace screensaver {

class RpsScreensaver final : public Screensaver {
public:
    void init(int width, int height, std::uint32_t now) override;
    bool update(std::uint32_t now) override;
    void draw(lgfx::LGFX_Sprite& canvas) const override;

private:
    enum class Kind { Rock, Scissors, Paper };
    struct Unit {
        float x = 0, y = 0, vx = 0, vy = 0;
        Kind kind                = Kind::Rock;
        std::uint32_t protection = 0;
        std::uint32_t flash      = 0;
    };
    void startRound();
    void collide(std::size_t i, std::size_t j);
    std::array<Unit, 12> _units{};
    std::array<bool, 3> _last_save_used{};
    std::array<std::array<bool, 12>, 12> _contacts{};
    int _max_x = 222, _max_y = 222;
    std::uint32_t _last_frame = 0;
    std::uint32_t _win_time   = 0;
    bool _won                 = false;
};

}  // namespace screensaver
