/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "cat_screensaver.h"

#include <M5GFX.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <assets/assets.h>
#include <tools/random/random.hpp>

namespace {

constexpr std::uint32_t TickInterval = 33;
constexpr int FrameSize              = 96;
constexpr int PixelScale             = 3;  // 1x source tiles (32px) -> 3x on-screen
constexpr float WalkSpeed            = 56.0F;
constexpr float RunSpeed             = 112.0F;
constexpr std::uint32_t WalkFrameMs  = 120;
constexpr std::uint32_t RunFrameMs   = 80;
constexpr std::uint32_t IdleFrameMs  = 350;
constexpr std::uint32_t CleanFrameMs = 220;
constexpr std::uint32_t PawFrameMs   = 150;
constexpr std::uint32_t SleepFrameMs = 550;
constexpr std::uint32_t JumpFrameMs  = 90;
// Chroma-key: transparent pixels in the frames are stored as pure green and
// skipped at blit time, so the background (canvas.clear) shows through. The cat
// artwork uses only four colors, none of which quantizes to this RGB565 value.
constexpr std::uint16_t ChromaKeySwapped = lgfx::swap565(0, 255, 0);

// The visible cat only occupies the lower part of each frame; the rest is
// background-colored padding baked into the sprite. These are the empty margins
// inside the frame (measured from the widest/tallest walk frame), so the cat's
// body — not its invisible padding — can reach the screen edges.
constexpr int CatLeftPad  = 21;  // empty pixels from frame left to the cat's body
constexpr int CatRightPad = 22;  // empty pixels from the cat's body to frame right
constexpr int CatTopPad   = 60;  // empty pixels from frame top to the cat's body
constexpr int EdgeReach   = 18;  // breathing room left at each border

// A cropped frame plus the offset of its content within the original 32x32 tile,
// in tile pixels. Drawing at (_draw_x + ox*PixelScale, _draw_y + oy*PixelScale)
// reproduces the exact on-screen body position the full tile used to occupy, so
// frames of different sizes stay aligned across an animation.
struct CatFrame {
    const gfx_image_dsc_t* image;
    int ox;
    int oy;
};

template <typename DestinationPixel>
void blitCatFrame(lgfx::LGFX_Sprite& canvas, const gfx_image_dsc_t& image, int base_x, int base_y)
{
    const auto* source           = reinterpret_cast<const lgfx::swap565_t*>(image.data);
    auto* destination            = reinterpret_cast<DestinationPixel*>(canvas.getBuffer());
    const int source_width       = image.width;
    const int source_height      = image.height;
    const int source_stride      = image.stride / sizeof(lgfx::swap565_t);
    const int canvas_width       = static_cast<int>(canvas.width());
    const int canvas_height      = static_cast<int>(canvas.height());
    const int destination_stride = canvas_width;

    for (int source_y = 0; source_y < source_height; ++source_y) {
        const int destination_y = base_y + source_y * PixelScale;
        if (destination_y + PixelScale <= 0 || destination_y >= canvas_height) {
            continue;
        }
        for (int source_x = 0; source_x < source_width; ++source_x) {
            const int destination_x = base_x + source_x * PixelScale;
            if (destination_x + PixelScale <= 0 || destination_x >= canvas_width) {
                continue;
            }

            const auto source_pixel = source[source_y * source_stride + source_x];
            if (source_pixel.raw == ChromaKeySwapped) {
                continue;
            }

            DestinationPixel destination_pixel;
            destination_pixel    = source_pixel;
            const int clipped_x0 = std::max(destination_x, 0);
            const int clipped_y0 = std::max(destination_y, 0);
            const int clipped_x1 = std::min(destination_x + PixelScale, canvas_width);
            const int clipped_y1 = std::min(destination_y + PixelScale, canvas_height);
            for (int y = clipped_y0; y < clipped_y1; ++y) {
                auto* row = destination + y * destination_stride;
                for (int x = clipped_x0; x < clipped_x1; ++x) {
                    row[x] = destination_pixel;
                }
            }
        }
    }
}

constexpr std::array<CatFrame, 8> WalkRightFrames  = {{
    {&image_cat_walk_right_0, 10, 21},
    {&image_cat_walk_right_1, 9, 20},
    {&image_cat_walk_right_2, 7, 20},
    {&image_cat_walk_right_3, 7, 20},
    {&image_cat_walk_right_4, 7, 21},
    {&image_cat_walk_right_5, 8, 20},
    {&image_cat_walk_right_6, 9, 20},
    {&image_cat_walk_right_7, 10, 20},
}};
constexpr std::array<CatFrame, 8> WalkLeftFrames   = {{
    {&image_cat_walk_left_0, 9, 21},
    {&image_cat_walk_left_1, 9, 20},
    {&image_cat_walk_left_2, 9, 20},
    {&image_cat_walk_left_3, 9, 20},
    {&image_cat_walk_left_4, 9, 21},
    {&image_cat_walk_left_5, 9, 20},
    {&image_cat_walk_left_6, 8, 20},
    {&image_cat_walk_left_7, 9, 20},
}};
constexpr std::array<CatFrame, 4> IdleRightFrames  = {{
    {&image_cat_idle_right_0, 9, 20},
    {&image_cat_idle_right_1, 9, 20},
    {&image_cat_idle_right_2, 9, 20},
    {&image_cat_idle_right_3, 9, 20},
}};
constexpr std::array<CatFrame, 4> IdleLeftFrames   = {{
    {&image_cat_idle_left_0, 11, 20},
    {&image_cat_idle_left_1, 11, 20},
    {&image_cat_idle_left_2, 11, 20},
    {&image_cat_idle_left_3, 11, 20},
}};
constexpr std::array<CatFrame, 4> SleepFrames      = {{
    {&image_cat_sleep_0, 8, 24},
    {&image_cat_sleep_1, 8, 24},
    {&image_cat_sleep_2, 8, 24},
    {&image_cat_sleep_3, 8, 24},
}};
constexpr std::array<CatFrame, 8> RunRightFrames   = {{
    {&image_cat_run_right_0, 10, 21},
    {&image_cat_run_right_1, 9, 20},
    {&image_cat_run_right_2, 7, 19},
    {&image_cat_run_right_3, 8, 20},
    {&image_cat_run_right_4, 9, 22},
    {&image_cat_run_right_5, 9, 21},
    {&image_cat_run_right_6, 10, 20},
    {&image_cat_run_right_7, 10, 21},
}};
constexpr std::array<CatFrame, 8> RunLeftFrames    = {{
    {&image_cat_run_left_0, 9, 21},
    {&image_cat_run_left_1, 8, 20},
    {&image_cat_run_left_2, 8, 19},
    {&image_cat_run_left_3, 8, 20},
    {&image_cat_run_left_4, 9, 22},
    {&image_cat_run_left_5, 9, 21},
    {&image_cat_run_left_6, 9, 20},
    {&image_cat_run_left_7, 9, 21},
}};
constexpr std::array<CatFrame, 4> CleanRightFrames = {{
    {&image_cat_clean_right_0, 9, 20},
    {&image_cat_clean_right_1, 9, 21},
    {&image_cat_clean_right_2, 9, 21},
    {&image_cat_clean_right_3, 9, 20},
}};
constexpr std::array<CatFrame, 4> CleanLeftFrames  = {{
    {&image_cat_clean_left_0, 10, 20},
    {&image_cat_clean_left_1, 10, 21},
    {&image_cat_clean_left_2, 10, 21},
    {&image_cat_clean_left_3, 10, 20},
}};
constexpr std::array<CatFrame, 6> PawRightFrames   = {{
    {&image_cat_paw_right_0, 10, 21},
    {&image_cat_paw_right_1, 10, 20},
    {&image_cat_paw_right_2, 11, 20},
    {&image_cat_paw_right_3, 10, 21},
    {&image_cat_paw_right_4, 10, 20},
    {&image_cat_paw_right_5, 10, 20},
}};
constexpr std::array<CatFrame, 6> PawLeftFrames    = {{
    {&image_cat_paw_left_0, 9, 21},
    {&image_cat_paw_left_1, 9, 20},
    {&image_cat_paw_left_2, 8, 20},
    {&image_cat_paw_left_3, 7, 21},
    {&image_cat_paw_left_4, 8, 20},
    {&image_cat_paw_left_5, 9, 20},
}};
constexpr std::array<CatFrame, 7> JumpRightFrames  = {{
    {&image_cat_jump_right_0, 9, 20},
    {&image_cat_jump_right_1, 9, 16},
    {&image_cat_jump_right_2, 9, 15},
    {&image_cat_jump_right_3, 9, 14},
    {&image_cat_jump_right_4, 9, 21},
    {&image_cat_jump_right_5, 9, 22},
    {&image_cat_jump_right_6, 9, 21},
}};
constexpr std::array<CatFrame, 7> JumpLeftFrames   = {{
    {&image_cat_jump_left_0, 11, 20},
    {&image_cat_jump_left_1, 8, 16},
    {&image_cat_jump_left_2, 8, 15},
    {&image_cat_jump_left_3, 9, 14},
    {&image_cat_jump_left_4, 10, 21},
    {&image_cat_jump_left_5, 10, 22},
    {&image_cat_jump_left_6, 10, 21},
}};

}  // namespace

namespace screensaver {

void CatScreensaver::init(int width, int height, std::uint32_t now)
{
    auto& random = smooth_ui_toolkit::Random::getInstance();
    _garden.init(now);
    _width       = width;
    _height      = height;
    _x           = static_cast<float>(std::max(0, width - FrameSize)) * 0.3F;
    _y           = static_cast<float>(std::max(0, height - FrameSize)) * 0.5F;
    _draw_x      = static_cast<int>(std::lround(_x));
    _draw_y      = static_cast<int>(std::lround(_y));
    _frame       = 0;
    _last_tick   = now;
    _frame_timer = 0;
    _state_timer = 0;
    _state       = State::Walk;
    _speed       = WalkSpeed;
    chooseDestination();
    _state_duration = random.getInt(3000, 7000);
}

void CatScreensaver::chooseDestination()
{
    auto& random       = smooth_ui_toolkit::Random::getInstance();
    const float left   = EdgeReach - CatLeftPad;
    const float right  = std::max(left, static_cast<float>(_width - FrameSize + CatRightPad - EdgeReach));
    const float top    = 24 - CatTopPad;
    const float bottom = std::max(top, static_cast<float>(_height - FrameSize - 14));
    _target_x          = (left + right) * 0.5F;
    _target_y          = (top + bottom) * 0.5F;
    for (int attempt = 0; attempt < 24; ++attempt) {
        const float x  = random.getFloat(left, right);
        const float y  = random.getFloat(top, bottom);
        const float dx = x - _x, dy = y - _y;
        const float distance = std::hypot(dx, dy);
        // Short, mostly sideways strolls suit the two-direction artwork.
        if (distance >= 42.0F && distance <= 125.0F && std::abs(dx) >= std::abs(dy) * 0.6F) {
            _target_x = x;
            _target_y = y;
            break;
        }
    }
    _facing_right = _target_x >= _x;
}

void CatScreensaver::startJump()
{
    const float left  = EdgeReach - CatLeftPad;
    const float right = std::max(left, static_cast<float>(_width - FrameSize + CatRightPad - EdgeReach));
    const float room  = _facing_right ? right - _x : _x - left;
    if (room < 45.0F) _facing_right = !_facing_right;
    const float distance = std::min(60.0F, std::max(0.0F, _facing_right ? right - _x : _x - left));
    _state               = State::Jump;
    _state_duration      = JumpFrameMs * JumpRightFrames.size();
    _target_x            = _x + (_facing_right ? distance : -distance);
    _target_y            = _y;
    _speed               = distance * 1000.0F / _state_duration;
}

bool CatScreensaver::update(std::uint32_t now)
{
    const std::uint32_t elapsed = now - _last_tick;
    if (elapsed < TickInterval) {
        return false;
    }
    _last_tick = now;
    // Pausing the simulator must not teleport the cat across the screen.
    const std::uint32_t dt = std::min(elapsed, std::uint32_t{100});
    _frame_timer += dt;
    _state_timer += dt;

    bool changed                 = _garden.update(now);
    const std::uint32_t frame_ms = [&]() {
        switch (_state) {
            case State::Walk:
                return WalkFrameMs;
            case State::Run:
                return RunFrameMs;
            case State::Idle:
                return IdleFrameMs;
            case State::Clean:
                return CleanFrameMs;
            case State::Paw:
                return PawFrameMs;
            case State::Sleep:
                return SleepFrameMs;
            case State::Jump:
                return JumpFrameMs;
        }
        return WalkFrameMs;
    }();
    const int frame_count = [&]() {
        switch (_state) {
            case State::Walk:
                return static_cast<int>(WalkRightFrames.size());
            case State::Run:
                return static_cast<int>(RunRightFrames.size());
            case State::Idle:
                return static_cast<int>(IdleRightFrames.size());
            case State::Clean:
                return static_cast<int>(CleanRightFrames.size());
            case State::Paw:
                return static_cast<int>(PawRightFrames.size());
            case State::Sleep:
                return static_cast<int>(SleepFrames.size());
            case State::Jump:
                return static_cast<int>(JumpRightFrames.size());
        }
        return static_cast<int>(WalkRightFrames.size());
    }();
    if (_frame_timer >= frame_ms) {
        _frame_timer -= frame_ms;
        _frame  = (_frame + 1) % frame_count;
        changed = true;
    }

    const bool moving = _state == State::Walk || _state == State::Run || _state == State::Jump;
    if (moving) {
        const float dx = _target_x - _x, dy = _target_y - _y;
        const float distance = std::hypot(dx, dy);
        // Ease into a chosen resting spot instead of colliding with a border.
        const float pace = _state == State::Jump ? _speed
                                                 : std::min(_speed, 16.0F + distance * 2.5F) *
                                                       std::min(1.0F, 0.35F + _state_timer / 300.0F);
        const float step = std::min(distance, pace * dt / 1000.0F);
        if (distance > 0.001F) {
            _x += dx / distance * step;
            _y += dy / distance * step;
        }
        if (distance <= step + 0.01F && _state != State::Jump) {
            _x = _target_x;
            _y = _target_y;
            transition();
            changed = true;
        }

        const int draw_x = static_cast<int>(std::lround(_x));
        const int draw_y = static_cast<int>(std::lround(_y));
        if (draw_x != _draw_x || draw_y != _draw_y) {
            _draw_x = draw_x;
            _draw_y = draw_y;
            changed = true;
        }
    }

    if (_state != State::Walk && _state != State::Run && _state_timer >= _state_duration) {
        transition();
        changed = true;
    }
    return changed;
}

void CatScreensaver::transition()
{
    const State previous = _state;
    _state_timer         = 0;
    _frame               = 0;
    _frame_timer         = 0;
    auto& random         = smooth_ui_toolkit::Random::getInstance();
    const int roll       = random.getInt(0, 99);
    if (previous == State::Walk || previous == State::Run) {
        // Reaching a destination is a chance to look around, groom or nap.
        _state = roll < 45 ? State::Idle : (roll < 70 ? State::Clean : (roll < 85 ? State::Paw : State::Sleep));
    } else if (previous == State::Sleep || previous == State::Jump) {
        _state = roll < 70 ? State::Idle : State::Walk;
    } else {
        _state = roll < 62
                     ? State::Walk
                     : (roll < 72 ? State::Run : (roll < 80 ? State::Jump : (roll < 90 ? State::Clean : State::Sleep)));
    }
    switch (_state) {
        case State::Walk:
        case State::Run:
            _speed = _state == State::Walk ? WalkSpeed : RunSpeed;
            chooseDestination();
            break;
        case State::Jump:
            startJump();
            break;
        case State::Idle:
            _state_duration = random.getInt(1500, 3500);
            break;
        case State::Clean:
            _state_duration = random.getInt(2000, 4500);
            break;
        case State::Paw:
            _state_duration = random.getInt(1500, 3500);
            break;
        case State::Sleep:
            _state_duration = random.getInt(5000, 12000);
            break;
    }
}

void CatScreensaver::drawShadow(lgfx::LGFX_Sprite& canvas) const
{
    // Anchor to the original tile's ground line, not the cropped frame bounds:
    // jump frames already contain the cat's vertical displacement.
    const bool airborne  = _state == State::Jump && _frame >= 1 && _frame <= 3;
    const int half_width = airborne ? 4 : (_state == State::Sleep ? 7 : 6);
    const int center_x   = _draw_x + 48;
    const int center_y   = _draw_y + 94;
    // On snow the shadow turns cool blue-grey instead of dark olive.
    const auto shadow = _garden.season() == CatGarden::Season::Winter ? 0x5A6A7AU : 0x454F36U;
    for (int row = -1; row <= 1; ++row) {
        const int span = half_width - (row == 0 ? 0 : 2);
        canvas.fillRectAlpha(center_x - span * PixelScale, center_y + row * PixelScale, span * 2 * PixelScale,
                             PixelScale, airborne ? 30 : 65, shadow);
    }
}

void CatScreensaver::draw(lgfx::LGFX_Sprite& canvas) const
{
    _garden.draw(canvas);
    drawShadow(canvas);
    const CatFrame* frame = nullptr;
    const auto& pick      = [&](const auto& right, const auto& left) -> const CatFrame& {
        return (_facing_right ? right : left)[_frame];
    };
    switch (_state) {
        case State::Walk:
            frame = &pick(WalkRightFrames, WalkLeftFrames);
            break;
        case State::Run:
            frame = &pick(RunRightFrames, RunLeftFrames);
            break;
        case State::Idle:
            frame = &pick(IdleRightFrames, IdleLeftFrames);
            break;
        case State::Clean:
            frame = &pick(CleanRightFrames, CleanLeftFrames);
            break;
        case State::Paw:
            frame = &pick(PawRightFrames, PawLeftFrames);
            break;
        case State::Sleep:
            frame = &SleepFrames[_frame];
            break;
        case State::Jump:
            frame = &pick(JumpRightFrames, JumpLeftFrames);
            break;
    }
    if (frame == nullptr) {
        return;
    }

    // Frames are stored cropped to their content at native 1x RGB565-swapped,
    // with transparent pixels keyed to pure green. Blit them into the canvas
    // at PixelScale x with nearest-neighbour, using the canvas-native RGB565
    // representation and offsetting by the content's position in its original
    // tile.
    const gfx_image_dsc_t* image = frame->image;
    const int base_x             = _draw_x + frame->ox * PixelScale;
    const int base_y             = _draw_y + frame->oy * PixelScale;
    if (canvas.getColorDepth() == lgfx::color_depth_t::rgb565_nonswapped) {
        blitCatFrame<lgfx::rgb565_t>(canvas, *image, base_x, base_y);
    } else {
        blitCatFrame<lgfx::swap565_t>(canvas, *image, base_x, base_y);
    }
    _garden.drawDust(canvas, true);
}

}  // namespace screensaver
