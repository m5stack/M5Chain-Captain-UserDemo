/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "about_page.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/audio/audio.h>
#include <cmath>
#include <cstdio>
#include <assets/assets.h>
#include <hal/hal.h>

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;

constexpr int MinimumWaterLevel = 0;
constexpr int MaximumWaterLevel = 100;
constexpr int WaterLevelStep    = 5;

constexpr std::uint32_t RenderInterval    = 16;
constexpr std::uint32_t WaveBoostHold     = 150;
constexpr float WaterVisualDuration       = 0.52F;
constexpr float WaterBounce               = 0.28F;
constexpr float FrontWaveAmplitude        = 10.0F;
constexpr float BackWaveAmplitude         = 13.0F;
constexpr float FrontWaveAmplitudeBoost   = 0.30F;
constexpr float BackWaveAmplitudeBoost    = 0.23F;
constexpr float FrontWaveLength           = 58.0F;
constexpr float BackWaveLength            = 76.0F;
constexpr float FrontWaveSpeed            = 48.0F;
constexpr float BackWaveSpeed             = 82.0F;
constexpr float WaveSpeedBoost            = 5.75F;
constexpr float WaveBoostRiseDuration     = 0.1F;
constexpr float WaveBoostFallDuration     = 1.15F;
constexpr float SurfaceTiltPeak           = 3.5F;
constexpr float SurfaceTiltVisualDuration = 0.46F;
constexpr float SurfaceTiltBounce         = 0.32F;

constexpr std::uint32_t BackgroundColor = 0xFFFFFF;
constexpr std::uint32_t FrontWaveColor  = 0xDFBDFE;
constexpr std::uint32_t BackWaveColor   = 0xF3E6FF;

constexpr std::uint32_t InkColor             = 0x6B496C;
constexpr std::uint32_t PixelHullColor       = 0x73698C;
constexpr std::uint32_t PixelHullLight       = 0xBDAACE;
constexpr std::uint32_t PixelMastColor       = 0x6E6874;
constexpr std::uint32_t PixelWindowColor     = 0xF7EFEF;
constexpr std::uint32_t PixelSubmarineColor  = 0x83789D;
constexpr std::uint32_t PixelPortholeColor   = 0xE7DCFB;
constexpr std::uint32_t PixelChargeBandColor = 0xDEC3AD;
constexpr std::uint32_t PixelBubbleColor     = 0xF7E7FF;
constexpr float GameWaterLevel               = 76.0F;
constexpr float ChargeSpeed                  = 66.0F;

constexpr float Pi               = 3.14159265F;
constexpr float TwoPi            = 2.0F * Pi;
constexpr float ParticleDuration = 0.7F;

constexpr float ReadyTextLiftStart = 48.0F;
constexpr float ReadyTextLift      = 21.0F;

constexpr int FinishedPanelWidth  = 168;
constexpr int FinishedPanelHeight = 76;
constexpr int FinishedPanelX      = (ScreenWidth - FinishedPanelWidth) / 2;
constexpr int FinishedPanelY      = (ScreenHeight - FinishedPanelHeight) / 2;
const lgfx::LVGLFont FaceFont{&font_noto_sans_semibold_32, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont ScoreFont{&font_noto_sans_semibold_32, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont SmallFont{&font_noto_sans_semibold_14, lgfx::lvgl_font_blend_t::destination};

const float FrontWaveStepSine   = std::sin(1.0F / FrontWaveLength);
const float FrontWaveStepCosine = std::cos(1.0F / FrontWaveLength);
const float BackWaveStepSine    = std::sin(1.0F / BackWaveLength);
const float BackWaveStepCosine  = std::cos(1.0F / BackWaveLength);

float waterLevelFor(int value)
{
    return static_cast<float>(ScreenHeight) * (1.0F - value / 100.0F);
}

void drawWave(lgfx::LGFX_Sprite& canvas, float waterLevel, float phase, float amplitude, float wavelength,
              float stepSine, float stepCosine, float verticalOffset, float surfaceTilt, std::uint32_t color)
{
    float wave_sine                 = std::sin(phase / wavelength);
    float wave_cosine               = std::cos(phase / wavelength);
    constexpr float ScreenHalfWidth = static_cast<float>(ScreenWidth - 1) * 0.5F;

    for (int x = 0; x < ScreenWidth; ++x) {
        const float horizontal_position = (static_cast<float>(x) - ScreenHalfWidth) / ScreenHalfWidth;
        const float top     = waterLevel + wave_sine * amplitude + verticalOffset + surfaceTilt * horizontal_position;
        const int solid_top = static_cast<int>(std::ceil(top));
        const int edge_y    = solid_top - 1;
        const float edge_coverage = static_cast<float>(solid_top) - top;
        if (edge_y >= 0 && edge_y < ScreenHeight && edge_coverage > 0.0F) {
            const auto edge_alpha =
                static_cast<std::uint8_t>(std::lround(std::clamp(edge_coverage, 0.0F, 1.0F) * 255.0F));
            canvas.fillRectAlpha(x, edge_y, 1, 1, edge_alpha, color);
        }

        const int clipped_solid_top = std::clamp(solid_top, 0, ScreenHeight);
        if (clipped_solid_top < ScreenHeight) {
            canvas.drawFastVLine(x, clipped_solid_top, ScreenHeight - clipped_solid_top, color);
        }

        const float next_sine = wave_sine * stepCosine + wave_cosine * stepSine;
        wave_cosine           = wave_cosine * stepCosine - wave_sine * stepSine;
        wave_sine             = next_sine;
    }
}

void configureWaterSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = WaterVisualDuration;
    options.bounce         = WaterBounce;
    options.restDelta      = 0.1F;
    options.restSpeed      = 0.1F;
    animation.delay        = 0.0F;
}

void configureWaveSpeedEasing(uitk::AnimateValue& animation, float duration)
{
    auto& options          = animation.easingOptions();
    options.duration       = duration;
    options.easingFunction = uitk::ease::ease_out_quad;
    animation.delay        = 0.0F;
}

void configureSurfaceTiltSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = SurfaceTiltVisualDuration;
    options.bounce         = SurfaceTiltBounce;
    options.restDelta      = 0.02F;
    options.restSpeed      = 0.02F;
    animation.delay        = 0.0F;
}

void fillPixelRect(lgfx::LGFX_Sprite& canvas, int originX, int originY, int spriteWidth, bool mirrored, int x, int y,
                   int width, int height, std::uint32_t color)
{
    const int draw_x = mirrored ? originX + spriteWidth - x - width : originX + x;
    canvas.fillRect(draw_x, originY + y, width, height, color);
}

void drawPixelWarship(lgfx::LGFX_Sprite& canvas, int centerX, int waterline)
{
    const int x = centerX - 22;
    const int y = waterline - 14;
    canvas.fillRect(x + 2, y + 11, 37, 8, PixelHullColor);
    canvas.fillRect(x + 39, y + 11, 4, 6, PixelHullColor);
    canvas.fillRect(x + 43, y + 11, 2, 4, PixelHullColor);
    canvas.fillRect(x, y + 11, 2, 6, PixelHullColor);
    canvas.fillRect(x + 10, y + 5, 13, 6, PixelHullLight);
    canvas.fillRect(x + 30, y + 10, 6, 1, PixelHullLight);
    canvas.fillRect(x + 32, y + 9, 6, 1, PixelHullLight);
    canvas.fillRect(x + 3, y + 13, 2, 1, PixelHullLight);
    canvas.fillRect(x + 7, y + 13, 2, 1, PixelHullLight);
    canvas.fillRect(x + 14, y, 2, 5, PixelMastColor);
    canvas.fillRect(x + 12, y + 7, 8, 2, PixelWindowColor);
}

void drawPixelSubmarine(lgfx::LGFX_Sprite& canvas, int centerX, int centerY, bool facingRight)
{
    constexpr int SpriteWidth = 33;
    const int x               = centerX - SpriteWidth / 2;
    const int y               = centerY - 12;
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 2, 7, 25, 9, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 30, 8, 3, 7, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 0, 8, 2, 7, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 27, 8, 2, 7, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 12, 4, 9, 3, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 16, 0, 2, 4, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 14, 0, 2, 2, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 29, 10, 1, 3, PixelSubmarineColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 5, 11, 2, 2, PixelPortholeColor);
    fillPixelRect(canvas, x, y, SpriteWidth, facingRight, 9, 11, 2, 2, PixelPortholeColor);
}

void drawPixelCharge(lgfx::LGFX_Sprite& canvas, int centerX, int centerY)
{
    const int x = centerX - 2;
    const int y = centerY - 4;
    canvas.fillRect(x, y, 4, 8, PixelHullColor);
    canvas.fillRect(x, y + 2, 4, 1, PixelChargeBandColor);
    canvas.fillRect(x + 1, y - 3, 1, 1, PixelBubbleColor);
    canvas.fillRect(x + 3, y - 6, 1, 1, PixelBubbleColor);
    canvas.fillRect(x, y - 6, 1, 1, PixelBubbleColor);
    canvas.fillRect(x + 2, y - 4, 1, 1, PixelBubbleColor);
    canvas.fillRect(x + 1, y - 5, 1, 1, PixelBubbleColor);
    canvas.fillRect(x + 2, y - 2, 1, 1, PixelBubbleColor);
}

}  // namespace

namespace settings {

void AboutPage::init()
{
    _random.seed(GetHAL().millis() | 1U);
    _game_state  = GameState::Ready;
    _value       = 25;
    _score       = 0;
    _boat_x      = 120;
    _boat_speed  = 0;
    _remaining   = 60;
    _launch_time = _shot_cooldown = _finished_time = 0;
    _movement_armed                                = false;
    _charges                                       = {};
    _bursts                                        = {};
    configureWaterSpring(_water_level);
    _water_level.teleport(static_cast<float>(ScreenHeight) + BackWaveAmplitude);
    _water_level = waterLevelFor(_value);

    configureWaveSpeedEasing(_wave_energy, WaveBoostRiseDuration);
    _wave_energy.teleport(0.0F);

    configureSurfaceTiltSpring(_surface_tilt);
    _surface_tilt.teleport(0.0F);

    _result               = SettingsPageResult::Active;
    _held_edge_direction  = 0;
    _last_update_time     = 0;
    _last_render_time     = 0;
    _front_wave_phase     = 0.0F;
    _back_wave_phase      = 0.0F;
    _last_change_at       = 0;
    _has_update_time      = false;
    _render_requested     = true;
    _wave_boost_requested = false;
    _wave_boost_active    = false;
    _wave_boost_settling  = false;
}

void AboutPage::handleInput(input::KeyEvent event)
{
    if (event == input::KeyEvent::Home) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        _result = SettingsPageResult::Cancelled;
        return;
    }
    if (_game_state == GameState::Finished) {
        if (event == input::KeyEvent::OkPressed && _finished_time >= 0.5F) {
            startGame();
            audio::play_ui_sfx(audio::UiSfx::Confirm);
        }
        return;
    }
    if (_game_state == GameState::Playing) {
        if (event == input::KeyEvent::OkPressed && _launch_time <= 0) dropCharge();
        return;
    }
    if (event == input::KeyEvent::OkPressed && _value == MaximumWaterLevel && _water_level.directValue() <= 8.0F) {
        startGame();
        audio::play_ui_sfx(audio::UiSfx::Confirm);
        return;
    }
    if (_held_edge_direction != 0 && !GetHAL().btnB.isPressed() && !GetHAL().btnC.isPressed()) {
        _held_edge_direction = 0;
    }

    switch (event) {
        case input::KeyEvent::Previous:
        case input::KeyEvent::PreviousRepeat: {
            if (setValue(_value - WaterLevelStep)) {
                _held_edge_direction = 0;
                audio::play_ui_sfx(audio::UiSfx::Navigate);
            } else if (_held_edge_direction == 0) {
                _held_edge_direction = -1;
                audio::play_ui_sfx(audio::UiSfx::Boundary);
            }
            break;
        }
        case input::KeyEvent::Next:
        case input::KeyEvent::NextRepeat: {
            if (setValue(_value + WaterLevelStep)) {
                _held_edge_direction = 0;
                audio::play_ui_sfx(audio::UiSfx::Navigate);
            } else if (_held_edge_direction == 0) {
                _held_edge_direction = 1;
                audio::play_ui_sfx(audio::UiSfx::Boundary);
            }
            break;
        }
        case input::KeyEvent::Home:
            audio::play_ui_sfx(audio::UiSfx::Back);
            _result = SettingsPageResult::Cancelled;
            break;
        default:
            break;
    }
}

void AboutPage::update(std::uint32_t currentTime)
{
    updateWaveMotion(currentTime);

    float elapsed_seconds = 0.0F;
    if (_has_update_time) {
        elapsed_seconds = std::min(static_cast<float>(currentTime - _last_update_time) / 1000.0F, 0.05F);
    }
    _last_update_time = currentTime;
    _has_update_time  = true;

    const float wave_energy      = _wave_energy.value();
    const float speed_multiplier = 1.0F + WaveSpeedBoost * wave_energy;
    _front_wave_phase += FrontWaveSpeed * speed_multiplier * elapsed_seconds;
    _back_wave_phase -= BackWaveSpeed * speed_multiplier * elapsed_seconds;

    updateGame(elapsed_seconds);

    if (!_render_requested && currentTime - _last_render_time < RenderInterval) {
        return;
    }

    const float water_level = _water_level.value();
    const float tilt        = _surface_tilt.value();
    draw(water_level, wave_energy, tilt);
    GetHAL().presentGfx();
    _last_render_time = currentTime;
    _render_requested = false;
}

void AboutPage::startGame()
{
    _game_state    = GameState::Playing;
    _score         = 0;
    _boat_x        = 120;
    _boat_speed    = 0;
    _remaining     = 60;
    _launch_time   = 0.8F;
    _shot_cooldown = _finished_time = 0;
    _movement_armed                 = false;
    _charges                        = {};
    _bursts                         = {};
    // Let the water settle into the playfield before starting the round clock.
    _water_level          = GameWaterLevel;
    _wave_energy          = 0;
    _surface_tilt         = 0;
    _wave_boost_requested = _wave_boost_active = _wave_boost_settling = false;
    for (std::size_t i = 0; i < _submarines.size(); ++i) {
        const float direction = (_random() & 1U) ? 1.0F : -1.0F;
        _submarines[i]        = {direction > 0 ? -28.0F - i * 55 : 268.0F + i * 55, 128.0F + i * 38,
                          direction * (25.0F + i * 12), 0, static_cast<unsigned>((i + 1) * 10)};
    }
    _render_requested = true;
}

float AboutPage::boatWaterline(float waterLevel, float waveEnergy, float surfaceTilt) const
{
    return waterLevel +
           std::sin((_front_wave_phase + _boat_x) / FrontWaveLength) * FrontWaveAmplitude *
               (1.0F + FrontWaveAmplitudeBoost * waveEnergy) +
           surfaceTilt * (_boat_x - 119.5F) / 119.5F;
}

void AboutPage::dropCharge()
{
    if (_shot_cooldown > 0) return;
    for (auto& charge : _charges) {
        if (charge.active) continue;
        charge = {
            _boat_x,
            boatWaterline(_water_level.directValue(), _wave_energy.directValue(), _surface_tilt.directValue()) + 6,
            true};
        _shot_cooldown = 0.28F;
        audio::play_ui_sfx(audio::UiSfx::Navigate);
        return;
    }
    audio::play_ui_sfx(audio::UiSfx::Boundary);
}

void AboutPage::updateGame(float dt)
{
    if (_game_state == GameState::Ready) return;
    if (_game_state == GameState::Finished) {
        _finished_time = std::min(_finished_time + dt, 1.0F);
        return;
    }
    // The key used to raise the water must be released before steering starts.
    if (!GetHAL().btnB.isPressed() && !GetHAL().btnC.isPressed()) _movement_armed = true;
    if (_launch_time > 0) {
        _launch_time = std::max(0.0F, _launch_time - dt);
        return;
    }
    _remaining = std::max(0.0F, _remaining - dt);
    if (_remaining == 0) {
        _game_state = GameState::Finished;
        _best       = std::max(_best, _score);
        _boat_speed = 0;
        return;
    }
    _shot_cooldown = std::max(0.0F, _shot_cooldown - dt);
    const int direction =
        _movement_armed ? static_cast<int>(GetHAL().btnC.isPressed()) - static_cast<int>(GetHAL().btnB.isPressed()) : 0;
    _boat_speed += (direction * 96.0F - _boat_speed) * (1.0F - std::exp(-dt / 0.10F));
    _boat_x = std::clamp(_boat_x + _boat_speed * dt, 18.0F, 222.0F);
    if ((_boat_x == 18 && _boat_speed < 0) || (_boat_x == 222 && _boat_speed > 0)) _boat_speed = 0;
    for (auto& burst : _bursts) {
        if (burst.age >= 1.0F) continue;
        burst.age = std::min(burst.age + dt, 1.0F);
        for (auto& particle : burst.particles) {
            particle.x += particle.vx * dt;
            particle.y += particle.vy * dt;
        }
    }
    for (auto& sub : _submarines) {
        if (sub.respawn > 0) {
            sub.respawn = std::max(0.0F, sub.respawn - dt);
            continue;
        }
        sub.x += sub.speed * dt;
        if ((sub.speed > 0 && sub.x > 260) || (sub.speed < 0 && sub.x < -20)) {
            sub.x       = sub.speed > 0 ? -25 : 265;
            sub.respawn = 0.25F + (_random() % 60) / 100.0F;
        }
    }
    for (auto& charge : _charges) {
        if (!charge.active) continue;
        const float previous_y = charge.y;
        charge.y += ChargeSpeed * dt;
        for (auto& sub : _submarines) {
            if (sub.respawn > 0 || std::abs(charge.x - sub.x) > 16 || previous_y > sub.y + 7 || charge.y < sub.y - 7)
                continue;
            charge.active = false;
            _score += sub.points;
            auto& burst = *std::max_element(_bursts.begin(), _bursts.end(),
                                            [](const Burst& a, const Burst& b) { return a.age < b.age; });
            burst.age   = 0;
            for (std::size_t i = 0; i < burst.particles.size(); ++i) {
                const float angle = TwoPi * static_cast<float>(i) / static_cast<float>(burst.particles.size()) +
                                    static_cast<float>(static_cast<int>(_random() % 100) - 50) * 0.01F;
                const float speed  = 12.0F + static_cast<float>(_random() % 14);
                burst.particles[i] = {charge.x, sub.y, std::cos(angle) * speed, std::sin(angle) * speed - 4.0F};
            }
            sub.respawn = 0.8F;
            sub.x       = sub.speed > 0 ? -25 : 265;
            audio::play_ui_sfx(audio::UiSfx::Confirm);
            break;
        }
        if (charge.y > ScreenHeight + 5) charge.active = false;
    }
}

SettingsPageResult AboutPage::result() const
{
    return _result;
}

TransitionSurface AboutPage::transitionSurface() const
{
    return TransitionSurface::Background;
}

bool AboutPage::setValue(int value)
{
    const int clamped_value = std::clamp(value, MinimumWaterLevel, MaximumWaterLevel);
    if (clamped_value == _value) {
        return false;
    }

    const int direction   = clamped_value > _value ? 1 : -1;
    _value                = clamped_value;
    _water_level          = waterLevelFor(_value);
    _surface_tilt         = static_cast<float>(direction) * SurfaceTiltPeak;
    _render_requested     = true;
    _wave_boost_requested = true;
    return true;
}

void AboutPage::updateWaveMotion(std::uint32_t currentTime)
{
    if (_wave_boost_requested) {
        configureWaveSpeedEasing(_wave_energy, WaveBoostRiseDuration);
        _wave_energy          = 1.0F;
        _last_change_at       = currentTime;
        _wave_boost_requested = false;
        _wave_boost_active    = true;
        _wave_boost_settling  = false;
        return;
    }

    if (_wave_boost_active && !_wave_boost_settling && currentTime - _last_change_at >= WaveBoostHold) {
        configureWaveSpeedEasing(_wave_energy, WaveBoostFallDuration);
        _wave_energy         = 0.0F;
        _surface_tilt        = 0.0F;
        _wave_boost_settling = true;
    }

    if (_wave_boost_settling && _wave_energy.done()) {
        _wave_boost_active   = false;
        _wave_boost_settling = false;
    }
}

void AboutPage::draw(float waterLevel, float waveEnergy, float surfaceTilt)
{
    auto& canvas = GetHAL().gfxCanvas();
    canvas.clear(BackgroundColor);

    const float back_amplitude  = BackWaveAmplitude * (1.0F + BackWaveAmplitudeBoost * waveEnergy);
    const float front_amplitude = FrontWaveAmplitude * (1.0F + FrontWaveAmplitudeBoost * waveEnergy);
    drawWave(canvas, waterLevel, _back_wave_phase, back_amplitude, BackWaveLength, BackWaveStepSine, BackWaveStepCosine,
             -10.0F, surfaceTilt, BackWaveColor);
    drawWave(canvas, waterLevel, _front_wave_phase, front_amplitude, FrontWaveLength, FrontWaveStepSine,
             FrontWaveStepCosine, 0.0F, surfaceTilt, FrontWaveColor);

    canvas.setTextSize(1);
    canvas.setTextColor(InkColor);
    canvas.setFont(&SmallFont);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    if (_game_state == GameState::Ready) {
        // Keep the blue-screen copy fixed until the water passes 80%, then
        // follow the actual animated water level so reversals stay continuous.
        const float rise      = std::clamp((ReadyTextLiftStart - waterLevel) / ReadyTextLiftStart, 0.0F, 1.0F);
        const int text_offset = -static_cast<int>(std::lround(ReadyTextLift * rise));
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.setFont(&FaceFont);
        canvas.setTextSize(1);
        canvas.drawString(":(", 22, 26 + text_offset);
        canvas.setTextSize(1);
        canvas.setFont(&SmallFont);
        canvas.drawString("Your device ran into", 24, 82 + text_offset);
        canvas.drawString("a problem. We're collecting", 24, 100 + text_offset);
        canvas.drawString("some error info...", 24, 118 + text_offset);
        char progress[24];
        const int complete = static_cast<int>(std::lround(std::clamp(100.0F - waterLevel / 2.4F, 0.0F, 100.0F)));
        std::snprintf(progress, sizeof(progress), "%d%% Complete", complete);
        canvas.drawString(progress, 24, 148 + text_offset);
        canvas.drawString("Stop code:", 24, 178 + text_offset);
        canvas.drawString("I_DONT_REALLY_KNOW", 24, 196 + text_offset);
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        // The label travels with the bottom of the water panel, hidden below
        // the screen until the user raises it all the way up.
        const int prompt_y = 219 + static_cast<int>(std::lround(std::max(0.0F, waterLevel)));
        canvas.drawString("Press OK to start", 120, prompt_y);
        return;
    }

    for (const auto& sub : _submarines) {
        if (sub.respawn > 0) continue;
        drawPixelSubmarine(canvas, static_cast<int>(std::lround(sub.x)), static_cast<int>(std::lround(sub.y)),
                           sub.speed > 0);
    }
    for (const auto& charge : _charges) {
        if (!charge.active) continue;
        const int x = static_cast<int>(std::lround(charge.x)), y = static_cast<int>(std::lround(charge.y));
        drawPixelCharge(canvas, x, y);
    }
    for (const auto& burst : _bursts) {
        if (burst.age >= ParticleDuration) continue;
        // Sine-squared envelope like the screensaver dust motes: the burst
        // fades in and out softly instead of popping in and hard-cutting out.
        const float envelope = std::sin(Pi * burst.age / ParticleDuration);
        const int alpha      = static_cast<int>(envelope * envelope * 216.0F) / 8 * 8;
        for (std::size_t i = 0; i < burst.particles.size(); ++i) {
            const auto& particle = burst.particles[i];
            const int size       = (i % 3 == 0) ? 6 : 4;
            canvas.fillRectAlpha(static_cast<int>(std::lround(particle.x)) - size / 2,
                                 static_cast<int>(std::lround(particle.y)) - size / 2, size, size,
                                 static_cast<std::uint8_t>(alpha), PixelHullColor);
        }
    }

    const int boat_x = static_cast<int>(std::lround(_boat_x));
    const int boat_y = static_cast<int>(std::lround(boatWaterline(waterLevel, waveEnergy, surfaceTilt)));
    drawPixelWarship(canvas, boat_x, boat_y);

    char text[40];
    if (_game_state == GameState::Finished) {
        canvas.fillRect(FinishedPanelX, FinishedPanelY, FinishedPanelWidth, FinishedPanelHeight, BackgroundColor);
        canvas.setFont(&ScoreFont);
        std::snprintf(text, sizeof(text), "%u", _score);
        canvas.drawString(text, ScreenWidth / 2, FinishedPanelY + 25);
        canvas.setFont(&SmallFont);
        std::snprintf(text, sizeof(text), "BEST %u", _best);
        canvas.drawString(text, ScreenWidth / 2, FinishedPanelY + 54);
    } else {
        canvas.setTextDatum(lgfx::textdatum_t::middle_left);
        std::snprintf(text, sizeof(text), "%03u", _score);
        canvas.drawString(text, 14, 22);
        canvas.setTextDatum(lgfx::textdatum_t::middle_right);
        std::snprintf(text, sizeof(text), "%02us", static_cast<unsigned>(std::ceil(_remaining)));
        canvas.drawString(text, 226, 22);
    }
}

}  // namespace settings
