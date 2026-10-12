/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "starry_night_screensaver.h"

#include <M5GFX.h>
#include <algorithm>
#include <cmath>
#include <tools/random/random.hpp>

namespace {

constexpr std::uint32_t FrameInterval  = 50;
constexpr std::uint32_t ChangeInterval = 200;
constexpr std::uint32_t RevealDuration = 8000;
constexpr std::uint32_t AnimationSpeed = 2;

struct Building {
    int x;
    int top;
    int width;
    int pattern;
    int brightness;
};

std::uint32_t lightColor(int level)
{
    return (static_cast<std::uint32_t>(level * 4 / 5) << 16) | (static_cast<std::uint32_t>(level * 15 / 16) << 8) |
           static_cast<std::uint32_t>(level);
}

}  // namespace

namespace screensaver {

void StarryNightScreensaver::init(int width, int height, std::uint32_t now)
{
    _skyline.clear();
    _meteor       = {};
    _meteor_wait  = 11000;
    _beacon_phase = 0;
    _beacon_on    = false;
    _beacon_x     = 0;
    _beacon_y     = 0;
    _stars.clear();
    _windows.clear();
    _last_frame  = now;
    _last_change = now;
    _reveal_age  = 0;
    if (width <= 0 || height <= 0) {
        return;
    }

    auto& random = smooth_ui_toolkit::Random::getInstance();
    std::vector<Building> buildings;
    // Black buildings have no visible outline: their window grids define the skyline.
    for (int x = 2; x < width - 4;) {
        const int building_width  = std::min(random.getInt(10, 21), width - x);
        const int building_height = random.getInt(height / 6, height * 2 / 5);
        buildings.push_back(
            {x, height - building_height, building_width, random.getInt(0, 3), random.getInt(190, 245)});
        x += building_width + random.getInt(-2, 3);
    }

    // Cache the silhouette only during generation; no additional framebuffer is needed.
    _skyline.assign(width, static_cast<std::int16_t>(height));
    _beacon_y = height;
    for (const auto& building : buildings) {
        for (int x = building.x; x < building.x + building.width; ++x) {
            _skyline[x] = static_cast<std::int16_t>(std::min<int>(_skyline[x], building.top));
        }
    }

    for (const auto& building : buildings) {
        if (building.top < _beacon_y) {
            _beacon_x = building.x + building.width / 2;
            _beacon_y = building.top - 2;
        }
    }

    _windows.reserve(width * height / 60);
    for (std::size_t index = 0; index < buildings.size(); ++index) {
        const auto& building    = buildings[index];
        const int window_width  = building.pattern == 0 ? 3 : (building.pattern == 1 ? 2 : 1);
        const int window_height = building.pattern == 3 ? 2 : 1;
        const int pitch_x       = window_width + (building.pattern == 2 ? 2 : 1);
        const int pitch_y       = window_height + (building.pattern == 0 ? 3 : 2);
        for (int y = building.top + 2; y + window_height < height; y += pitch_y) {
            for (int x = building.x + 2; x + window_width <= building.x + building.width - 1; x += pitch_x) {
                // Foreground buildings mask rear windows, including partially covered cells.
                bool covered = false;
                for (std::size_t next = index + 1; next < buildings.size(); ++next) {
                    const auto& front = buildings[next];
                    if (x < front.x + front.width && x + window_width > front.x && y + window_height > front.top) {
                        covered = true;
                        break;
                    }
                }
                if (covered) {
                    continue;
                }
                const auto nominal =
                    static_cast<std::uint8_t>(std::clamp(building.brightness + random.getInt(-35, 20), 65, 250));
                const auto target = static_cast<std::uint8_t>(random.getInt(0, 99) < 78 ? nominal : 0);
                _windows.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                                    static_cast<std::uint16_t>(random.getInt(0, RevealDuration)),
                                    static_cast<std::uint8_t>(window_width), static_cast<std::uint8_t>(window_height),
                                    nominal, target});
            }
        }
    }

    const int star_count = width * height / 72;
    _stars.reserve(star_count);
    // A dense but separated field of single pixels, with no stars showing through buildings.
    for (int attempt = 0; attempt < star_count * 12 && static_cast<int>(_stars.size()) < star_count; ++attempt) {
        const int x = random.getInt(1, std::max(1, width - 2));
        const int y = random.getInt(1, std::max(1, height - 2));
        if (x >= width || y >= _skyline[x] - 2) {
            continue;
        }
        const bool crowded = std::any_of(_stars.begin(), _stars.end(), [x, y](const Light& star) {
            return std::abs(x - star.x) <= 1 && std::abs(y - star.y) <= 1;
        });
        if (crowded) {
            continue;
        }
        const auto nominal = static_cast<std::uint8_t>(random.getInt(100, 255));
        _stars.push_back({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                          static_cast<std::uint16_t>(random.getInt(0, RevealDuration)), 1, 1, nominal, nominal});
    }
}

bool StarryNightScreensaver::updateLights(std::vector<Light>& lights, int step)
{
    bool changed = false;
    for (auto& light : lights) {
        if (_reveal_age < light.revealAt) {
            continue;
        }
        const int next = light.level < light.target ? std::min<int>(light.target, light.level + step)
                                                    : std::max<int>(light.target, light.level - step);
        changed |= next != light.level;
        light.level = static_cast<std::uint8_t>(next);
    }
    return changed;
}

bool StarryNightScreensaver::update(std::uint32_t now)
{
    const auto elapsed = now - _last_frame;
    if (elapsed < FrameInterval) {
        return false;
    }
    _last_frame                  = now;
    const auto animation_elapsed = std::min(elapsed, std::uint32_t{1000}) * AnimationSpeed;
    _reveal_age += std::min(animation_elapsed, RevealDuration - _reveal_age);

    if (_reveal_age == RevealDuration && now - _last_change >= ChangeInterval / AnimationSpeed) {
        _last_change = now;
        auto& random = smooth_ui_toolkit::Random::getInstance();
        if (!_stars.empty()) {
            for (int count = 0; count < 8; ++count) {
                auto& star  = _stars[random.getInt(0, static_cast<int>(_stars.size()) - 1)];
                star.target = static_cast<std::uint8_t>(random.getInt(0, 99) < 30 ? random.getInt(0, 40)
                                                                                  : random.getInt(140, 255));
            }
        }
        if (!_windows.empty()) {
            for (int count = 0; count < 5; ++count) {
                auto& window  = _windows[random.getInt(0, static_cast<int>(_windows.size()) - 1)];
                window.target = random.getInt(0, 99) < 75 ? window.nominal : 0;
            }
        }
    }
    const bool old_beacon      = _beacon_on;
    _beacon_phase              = (_beacon_phase + elapsed % 3400) % 3400;
    _beacon_on                 = _reveal_age == RevealDuration && _beacon_phase < 1700 && !_skyline.empty();
    const bool meteor_changed  = updateMeteor(animation_elapsed);
    const int fade_speed       = _reveal_age < RevealDuration ? 180 : 650;
    const int step             = static_cast<int>(animation_elapsed * fade_speed / 1000);
    const bool stars_changed   = updateLights(_stars, step);
    const bool windows_changed = updateLights(_windows, step);
    return stars_changed || windows_changed || old_beacon != _beacon_on || meteor_changed;
}

bool StarryNightScreensaver::updateMeteor(std::uint32_t elapsed)
{
    if (_skyline.empty()) {
        return false;
    }
    auto& random = smooth_ui_toolkit::Random::getInstance();
    if (!_meteor.active) {
        if (elapsed < _meteor_wait) {
            _meteor_wait -= elapsed;
            return false;
        }
        const int width   = static_cast<int>(_skyline.size());
        const bool right  = random.getInt(0, 1) != 0;
        _meteor.x         = width * (right ? 0.15F : 0.85F);
        _meteor.y         = random.getFloat(8.0F, std::max(8.0F, _beacon_y * 0.55F));
        _meteor.velocityX = random.getFloat(130.0F, 190.0F) * (right ? 1.0F : -1.0F);
        _meteor.velocityY = random.getFloat(45.0F, 85.0F);
        _meteor.duration  = random.getInt(600, 1000);
        _meteor.age       = 0;
        _meteor.active    = true;
        return true;
    }

    const auto dt = std::min(elapsed, std::uint32_t{100} * AnimationSpeed);
    _meteor.age += dt;
    _meteor.x += _meteor.velocityX * dt / 1000.0F;
    _meteor.y += _meteor.velocityY * dt / 1000.0F;
    const int x = static_cast<int>(std::lround(_meteor.x));
    if (_meteor.age >= _meteor.duration || x < 0 || x >= static_cast<int>(_skyline.size()) ||
        _meteor.y >= _skyline[x] - 1) {
        _meteor.active = false;
        _meteor_wait   = random.getInt(10000, 25000);
    }
    return true;
}

void StarryNightScreensaver::draw(lgfx::LGFX_Sprite& canvas) const
{
    canvas.clear(TFT_BLACK);
    const auto draw_lights = [&canvas](const std::vector<Light>& lights) {
        for (const auto& light : lights) {
            if (light.level != 0) {
                canvas.fillRect(light.x, light.y, light.width, light.height, lightColor(light.level));
            }
        }
    };
    draw_lights(_stars);
    draw_lights(_windows);
    if (_beacon_on) {
        canvas.drawPixel(_beacon_x, _beacon_y, 0xFF3838U);
        canvas.drawPixel(_beacon_x + 1, _beacon_y, 0xB81818U);
    }
    if (_meteor.active) {
        const float speed = std::hypot(_meteor.velocityX, _meteor.velocityY);
        const float fade  = std::min(1.0F, (_meteor.duration - _meteor.age) / 180.0F);
        for (int tail = 13; tail >= 0; --tail) {
            const int x = static_cast<int>(std::lround(_meteor.x - tail * _meteor.velocityX / speed));
            const int y = static_cast<int>(std::lround(_meteor.y - tail * _meteor.velocityY / speed));
            if (x >= 0 && x < static_cast<int>(_skyline.size()) && y >= 0 && y < _skyline[x] - 1) {
                const int level = static_cast<int>((255 - tail * 17) * fade);
                canvas.drawPixel(x, y, lightColor(level));
            }
        }
    }
}

}  // namespace screensaver
