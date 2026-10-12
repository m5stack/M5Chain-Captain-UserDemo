/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "magic_view.h"

#include <algorithm>
#include <assets/assets.h>
#include <cstdio>
#include <hal/hal.h>
#include <inttypes.h>

namespace {

constexpr int ScreenSize      = 240;
constexpr int PlayerWidth     = 14;
constexpr int PlayerHeight    = 14;
constexpr float Gravity       = 0.25F;
constexpr float JumpVelocity  = -7.6F;
constexpr float MaxFrameScale = 2.5F;

// The bubble level palette keeps the game feeling like part of the IMU app.
constexpr std::uint32_t Paper = 0xFDFCE7;
constexpr std::uint32_t Ink   = 0x994D24;
constexpr std::uint32_t Clay  = 0xD37744;

constexpr int FinishedPanelWidth  = 168;
constexpr int FinishedPanelHeight = 76;
constexpr int FinishedPanelX      = (ScreenSize - FinishedPanelWidth) / 2;
constexpr int FinishedPanelY      = (ScreenSize - FinishedPanelHeight) / 2;

const lgfx::LVGLFont ScoreFont{&font_noto_sans_semibold_32, lgfx::lvgl_font_blend_t::destination};
const lgfx::LVGLFont SmallFont{&font_noto_sans_semibold_14, lgfx::lvgl_font_blend_t::destination};

float clampTilt(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

}  // namespace

namespace view {

void MagicView::init()
{
    _random_state         = 0x4D414749U ^ (GetHAL().millis() * 2654435761U);
    _last_update          = GetHAL().millis();
    _magic_score          = 0;
    _magic_score_progress = 0.0F;
    _player_x             = 113.0F;
    _player_y             = 190.0F;
    _player_velocity_x    = 0.0F;
    _player_velocity_y    = JumpVelocity;
    _game_over            = false;
    seedPlatforms();
}

void MagicView::reset()
{
    init();
}

void MagicView::seedPlatforms()
{
    _platforms[0] = {92.0F, 220.0F, 58, 6};
    for (std::size_t index = 1; index < _platforms.size(); ++index) {
        const auto& previous = _platforms[index - 1];
        const int width      = 44 + static_cast<int>(nextRandom() % 24U);
        const int x          = static_cast<int>(nextRandom() % static_cast<std::uint32_t>(ScreenSize - width));
        const int gap        = 30 + static_cast<int>(nextRandom() % 16U);
        _platforms[index]    = {static_cast<float>(x), previous.y - static_cast<float>(gap), width, 6};
    }
}

std::uint32_t MagicView::nextRandom()
{
    _random_state = _random_state * 1664525U + 1013904223U;
    return _random_state;
}

void MagicView::recyclePlatform(Platform& platform)
{
    float highest = platform.y;
    for (const auto& candidate : _platforms) {
        if (&candidate != &platform) {
            highest = std::min(highest, candidate.y);
        }
    }
    platform.width = 44 + static_cast<int>(nextRandom() % 24U);
    platform.x     = static_cast<float>(nextRandom() % static_cast<std::uint32_t>(ScreenSize - platform.width));
    platform.y     = highest - 32.0F - static_cast<float>(nextRandom() % 13U);
}

void MagicView::update(std::uint32_t currentTime, const ImuData& data)
{
    if (_game_over) {
        return;
    }

    const float frame_scale = std::min(static_cast<float>(currentTime - _last_update) / 16.0F, MaxFrameScale);
    _last_update            = currentTime;
    if (frame_scale <= 0.0F) {
        return;
    }

    const float tilt             = clampTilt(data.accelX);
    const float desired_velocity = -tilt * 3.2F;
    _player_velocity_x += (desired_velocity - _player_velocity_x) * std::min(1.0F, 0.18F * frame_scale);
    _player_x += _player_velocity_x * frame_scale;
    if (_player_x < -static_cast<float>(PlayerWidth)) {
        _player_x = static_cast<float>(ScreenSize);
    } else if (_player_x > static_cast<float>(ScreenSize)) {
        _player_x = -static_cast<float>(PlayerWidth);
    }

    const float old_y = _player_y;
    _player_velocity_y += Gravity * frame_scale;
    _player_y += _player_velocity_y * frame_scale;
    if (_player_velocity_y < 0.0F) {
        _magic_score_progress += std::max(0.0F, old_y - _player_y);
        _magic_score = static_cast<std::uint32_t>(_magic_score_progress);
    }

    if (_player_velocity_y > 0.0F) {
        const float old_bottom = old_y + static_cast<float>(PlayerHeight);
        const float new_bottom = _player_y + static_cast<float>(PlayerHeight);
        for (auto& platform : _platforms) {
            const bool crossed = old_bottom <= platform.y && new_bottom >= platform.y;
            const bool overlaps =
                _player_x + PlayerWidth - 2.0F >= platform.x && _player_x + 2.0F <= platform.x + platform.width;
            if (crossed && overlaps) {
                _player_y          = platform.y - static_cast<float>(PlayerHeight);
                _player_velocity_y = JumpVelocity;
                break;
            }
        }
    }

    if (_player_y < 88.0F && _player_velocity_y < 0.0F) {
        const float scroll = 88.0F - _player_y;
        _player_y          = 88.0F;
        for (auto& platform : _platforms) {
            platform.y += scroll;
        }
    }

    for (auto& platform : _platforms) {
        if (platform.y > ScreenSize + 12.0F) {
            recyclePlatform(platform);
        }
    }

    if (_player_y > ScreenSize + 12.0F) {
        _game_over  = true;
        _magic_best = std::max(_magic_best, _magic_score);
    }
}

void MagicView::draw(lgfx::LGFX_Sprite& canvas)
{
    canvas.fillScreen(Paper);

    for (const auto& platform : _platforms) {
        const int x = static_cast<int>(platform.x);
        const int y = static_cast<int>(platform.y);
        if (y < -8 || y > ScreenSize) {
            continue;
        }
        canvas.fillRect(x, y, platform.width, platform.height, Clay);
    }

    const int player_x = static_cast<int>(_player_x);
    const int player_y = static_cast<int>(_player_y);
    canvas.fillRect(player_x, player_y, PlayerWidth, PlayerHeight, Ink);

    char text[24] = {};
    canvas.setTextColor(Ink);
    canvas.setTextSize(1);
    if (_game_over) {
        canvas.fillRect(FinishedPanelX, FinishedPanelY, FinishedPanelWidth, FinishedPanelHeight, Paper);
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        canvas.setFont(&ScoreFont);
        std::snprintf(text, sizeof(text), "%" PRIu32, _magic_score);
        canvas.drawString(text, ScreenSize / 2, FinishedPanelY + 25);
        canvas.setFont(&SmallFont);
        std::snprintf(text, sizeof(text), "BEST %" PRIu32, _magic_best);
        canvas.drawString(text, ScreenSize / 2, FinishedPanelY + 54);
    } else {
        canvas.setFont(&SmallFont);
        canvas.setTextDatum(lgfx::textdatum_t::middle_left);
        std::snprintf(text, sizeof(text), "%" PRIu32, _magic_score);
        canvas.drawString(text, 14, 22);
    }
}

}  // namespace view
