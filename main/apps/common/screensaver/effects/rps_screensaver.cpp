/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "rps_screensaver.h"
#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <tools/random/random.hpp>

namespace {
constexpr int Size                    = 18;
constexpr float Speed                 = 36.0F;
constexpr std::uint32_t FrameInterval = 30;
void bounce(float& position, float& velocity, int limit)
{
    if (position < 0) {
        position = 0;
        velocity = std::abs(velocity);
    } else if (position > limit) {
        position = limit;
        velocity = -std::abs(velocity);
    }
}
}  // namespace

namespace screensaver {
void RpsScreensaver::init(int width, int height, std::uint32_t now)
{
    _max_x      = std::max(0, width - Size);
    _max_y      = std::max(0, height - Size);
    _last_frame = now;
    startRound();
}

void RpsScreensaver::startRound()
{
    auto& random               = smooth_ui_toolkit::Random::getInstance();
    _won                       = false;
    _contacts                  = {};
    _last_save_used            = {};
    std::array<Kind, 3> groups = {Kind::Rock, Kind::Scissors, Kind::Paper};
    for (int i = 2; i > 0; --i) std::swap(groups[i], groups[random.getInt(0, i)]);
    constexpr float centers[3][2] = {{0.22F, 0.22F}, {0.78F, 0.22F}, {0.50F, 0.78F}};
    const float center_x          = (_max_x + Size) * 0.5F;
    const float center_y          = (_max_y + Size) * 0.5F;
    const float turn              = random.getInt(0, 1) ? 1.0F : -1.0F;
    for (std::size_t i = 0; i < _units.size(); ++i) {
        auto& unit       = _units[i];
        const auto group = i / 4;
        const auto slot  = i % 4;
        // Four matching icons form a loose 2x2 cluster. Jitter remains
        // smaller than the gap so no battle starts from an initial overlap.
        unit.x =
            (_max_x + Size) * centers[group][0] - Size / 2 + (slot % 2 ? 12.0F : -12.0F) + random.getFloat(-2.0F, 2.0F);
        unit.y =
            (_max_y + Size) * centers[group][1] - Size / 2 + (slot / 2 ? 12.0F : -12.0F) + random.getFloat(-2.0F, 2.0F);
        // Launch every unit into a shared tangential orbit around the screen
        // centre so the three kinds sweep into a balanced spiral chase. A
        // little jitter keeps the vortex from moving in perfect lockstep.
        const float dx = unit.x - center_x, dy = unit.y - center_y;
        const float length = std::hypot(dx, dy);
        const float jitter = random.getFloat(-0.2F, 0.2F);
        const float cos_a = std::cos(jitter), sin_a = std::sin(jitter);
        const float tx  = turn * -dy / length;
        const float ty  = turn * dx / length;
        unit.vx         = Speed * (tx * cos_a - ty * sin_a);
        unit.vy         = Speed * (tx * sin_a + ty * cos_a);
        unit.kind       = groups[group];
        unit.protection = unit.flash = 0;
    }
}

void RpsScreensaver::collide(std::size_t i, std::size_t j)
{
    auto& a        = _units[i];
    auto& b        = _units[j];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float overlap_x = Size - std::abs(dx), overlap_y = Size - std::abs(dy);
    if (overlap_x <= 0 || overlap_y <= 0) {
        if (overlap_x < -0.5F || overlap_y < -0.5F) _contacts[i][j] = false;
        return;
    }
    if (!_contacts[i][j] && a.kind != b.kind && a.protection == 0 && b.protection == 0) {
        // A converted unit cannot attack or be converted for 0.8 seconds.
        const bool a_wins      = (static_cast<int>(a.kind) + 1) % 3 == static_cast<int>(b.kind);
        auto& loser            = a_wins ? b : a;
        const Kind winner      = a_wins ? a.kind : b.kind;
        const auto losing_kind = static_cast<std::size_t>(loser.kind);
        const auto count =
            std::count_if(_units.begin(), _units.end(), [&](const Unit& unit) { return unit.kind == loser.kind; });
        if (count == 1 && !_last_save_used[losing_kind]) {
            _last_save_used[losing_kind] = true;
            loser.flash                  = 400;
        } else {
            loser.kind       = winner;
            loser.protection = 800;
            loser.flash      = 0;
            // The winner now has at least two members, rearming its next last stand.
            _last_save_used[static_cast<std::size_t>(winner)] = false;
        }
    }
    _contacts[i][j] = true;
    if (overlap_x < overlap_y) {
        const float normal = dx >= 0 ? 1.0F : -1.0F;
        a.x -= normal * (overlap_x + 0.1F) * 0.5F;
        b.x += normal * (overlap_x + 0.1F) * 0.5F;
        if ((a.vx - b.vx) * normal > 0) std::swap(a.vx, b.vx);
    } else {
        const float normal = dy >= 0 ? 1.0F : -1.0F;
        a.y -= normal * (overlap_y + 0.1F) * 0.5F;
        b.y += normal * (overlap_y + 0.1F) * 0.5F;
        if ((a.vy - b.vy) * normal > 0) std::swap(a.vy, b.vy);
    }
}

bool RpsScreensaver::update(std::uint32_t now)
{
    if (_won) {
        if (now - _win_time < 2000) return false;
        startRound();
        _last_frame = now;
        return true;
    }
    const auto elapsed = now - _last_frame;
    if (elapsed < FrameInterval) return false;
    _last_frame = now;
    // Substeps prevent fast frames from tunneling through other icons.
    const auto dt = std::min(elapsed, std::uint32_t{100});
    for (auto& unit : _units) {
        unit.protection -= std::min(unit.protection, dt);
        unit.flash -= std::min(unit.flash, dt);
    }
    const int steps     = (dt + 14) / 15;
    const float seconds = dt / (1000.0F * steps);
    for (int step = 0; step < steps; ++step) {
        for (auto& unit : _units) {
            unit.x += unit.vx * seconds;
            unit.y += unit.vy * seconds;
            bounce(unit.x, unit.vx, _max_x);
            bounce(unit.y, unit.vy, _max_y);
        }
        for (std::size_t i = 0; i < _units.size(); ++i)
            for (std::size_t j = i + 1; j < _units.size(); ++j) collide(i, j);
        for (auto& unit : _units) {
            bounce(unit.x, unit.vx, _max_x);
            bounce(unit.y, unit.vy, _max_y);
        }
    }
    _won = std::all_of(_units.begin(), _units.end(), [&](const Unit& unit) { return unit.kind == _units[0].kind; });
    if (_won) {
        _win_time = now;
    }
    return true;
}

void RpsScreensaver::draw(lgfx::LGFX_Sprite& canvas) const
{
    constexpr std::array<const gfx_image_dsc_t*, 3> Images = {&image_rps_rock, &image_rps_scissors, &image_rps_paper};
    canvas.clear(TFT_BLACK);
    for (const auto& unit : _units) {
        const auto* image = Images[static_cast<int>(unit.kind)];
        canvas.pushImage(static_cast<int>(std::lround(unit.x)), static_cast<int>(std::lround(unit.y)), Size, Size,
                         reinterpret_cast<const lgfx::swap565_t*>(image->data));
        const auto timer = unit.flash;
        if (timer && (timer / 80) % 2 == 0) {
            const int x               = static_cast<int>(std::lround(unit.x));
            const int y               = static_cast<int>(std::lround(unit.y));
            const std::uint32_t color = 0xFFE699U;
            for (int corner : {0, Size - 3}) {
                canvas.fillRect(x + corner, y, 3, 1, color);
                canvas.fillRect(x + corner, y + Size - 1, 3, 1, color);
            }
        }
    }
}
}  // namespace screensaver
