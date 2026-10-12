/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <M5GFX.h>
#include <array>
#include <cstdint>

struct ImuData;

namespace view {

class MagicView final {
public:
    void init();
    void reset();
    void update(std::uint32_t currentTime, const ImuData& data);
    void draw(lgfx::LGFX_Sprite& canvas);

    bool isGameOver() const
    {
        return _game_over;
    }

private:
    struct Platform {
        float x    = 0.0F;
        float y    = 0.0F;
        int width  = 54;
        int height = 6;
    };

    void seedPlatforms();
    void recyclePlatform(Platform& platform);
    std::uint32_t nextRandom();

    std::array<Platform, 8> _platforms = {};
    std::uint32_t _random_state        = 0x4D414749U;
    std::uint32_t _last_update         = 0;
    std::uint32_t _magic_score         = 0;
    std::uint32_t _magic_best          = 0;
    float _magic_score_progress        = 0.0F;
    float _player_x                    = 113.0F;
    float _player_y                    = 190.0F;
    float _player_velocity_x           = 0.0F;
    float _player_velocity_y           = -7.6F;
    bool _game_over                    = false;
};

}  // namespace view
