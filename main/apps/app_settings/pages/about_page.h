/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace settings {

class AboutPage final : public SettingsPage {
public:
    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;
    TransitionSurface transitionSurface() const override;

private:
    enum class GameState { Ready, Playing, Finished };
    struct Submarine {
        float x         = 0;
        float y         = 0;
        float speed     = 0;
        float respawn   = 0;
        unsigned points = 10;
    };
    struct Charge {
        float x = 0, y = 0;
        bool active = false;
    };
    struct Burst {
        static constexpr std::size_t ParticleCount = 10;
        struct Particle {
            float x = 0, y = 0, vx = 0, vy = 0;
        };
        float age = 1;
        std::array<Particle, ParticleCount> particles{};
    };
    void startGame();
    void dropCharge();
    void updateGame(float dt);
    float boatWaterline(float waterLevel, float waveEnergy, float surfaceTilt) const;
    bool setValue(int value);
    void updateWaveMotion(std::uint32_t currentTime);
    void draw(float waterLevel, float waveEnergy, float surfaceTilt);

    std::array<Submarine, 3> _submarines{};
    std::array<Charge, 4> _charges{};
    std::array<Burst, 4> _bursts{};
    std::minstd_rand _random{1};
    GameState _game_state = GameState::Ready;
    unsigned _score       = 0;
    unsigned _best        = 0;
    float _boat_x         = 120;
    float _boat_speed     = 0;
    float _remaining      = 60;
    float _launch_time    = 0;
    float _shot_cooldown  = 0;
    float _finished_time  = 0;
    bool _movement_armed  = false;
    uitk::AnimateValue _water_level;
    uitk::AnimateValue _wave_energy;
    uitk::AnimateValue _surface_tilt;
    int _value                      = 50;
    int _held_edge_direction        = 0;
    SettingsPageResult _result      = SettingsPageResult::Active;
    std::uint32_t _last_update_time = 0;
    std::uint32_t _last_render_time = 0;
    std::uint32_t _last_change_at   = 0;
    float _front_wave_phase         = 0.0F;
    float _back_wave_phase          = 0.0F;
    bool _has_update_time           = false;
    bool _render_requested          = true;
    bool _wave_boost_requested      = false;
    bool _wave_boost_active         = false;
    bool _wave_boost_settling       = false;
};

}  // namespace settings
