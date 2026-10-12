/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "page_transition.h"

#include <M5GFX.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <hal/hal.h>

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;
constexpr int RootTravel   = 48;
constexpr int PanelExtra   = 30;
constexpr int PanelWidth   = ScreenWidth + PanelExtra;
constexpr int PanelRadius  = 30;

constexpr float TransitionVisualDuration = 0.32F;
constexpr float TransitionBounce         = 0.10F;
constexpr std::uint32_t RenderIntervalMs = 16;

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

}  // namespace

namespace settings::view {

PageTransition::PageTransition() = default;

PageTransition::~PageTransition() = default;

bool PageTransition::captureRoot()
{
    if (!ensureBuffers()) {
        return false;
    }
    copyCanvasTo(*_root_snapshot);
    return true;
}

bool PageTransition::capturePage()
{
    if (!_buffers_ready) {
        return false;
    }
    copyCanvasTo(*_page_snapshot);
    return true;
}

void PageTransition::beginPush(TransitionSurface surface, std::uint32_t backgroundColor)
{
    _direction             = Direction::Push;
    _page_surface          = surface;
    _page_background_color = backgroundColor;
    configureAnimation();
}

void PageTransition::beginPop(std::uint32_t backgroundColor)
{
    _direction             = Direction::Pop;
    _page_surface          = TransitionSurface::Snapshot;
    _page_background_color = backgroundColor;
    configureAnimation();
}

std::optional<PageTransition::Direction> PageTransition::update(std::uint32_t currentTime)
{
    if (!_active) {
        return std::nullopt;
    }

    _progress.update(static_cast<float>(currentTime) / 1000.0F);
    if (_last_render_time == 0 || currentTime - _last_render_time >= RenderIntervalMs || _progress.done()) {
        compose(_progress.directValue());
        GetHAL().presentGfx();
        _last_render_time = currentTime;
    }

    if (!_progress.done()) {
        return std::nullopt;
    }

    const Direction completed_direction = _direction;
    _active                             = false;
    return completed_direction;
}

bool PageTransition::active() const
{
    return _active;
}

void PageTransition::reset()
{
    _active           = false;
    _last_render_time = 0;
    _root_snapshot.reset();
    _page_snapshot.reset();
    _buffers_ready = false;
    _progress.pause();
}

bool PageTransition::ensureBuffers()
{
    if (_buffers_ready) {
        return true;
    }

    auto create_snapshot = []() {
        auto snapshot = std::make_unique<lgfx::LGFX_Sprite>(&GetHAL().gfxCanvas());
        snapshot->setColorDepth(lgfx::color_depth_t::rgb565_2Byte);
        snapshot->setPsram(true);
        if (snapshot->createSprite(ScreenWidth, ScreenHeight) == nullptr) {
            return std::unique_ptr<lgfx::LGFX_Sprite>{};
        }
        return snapshot;
    };

    _root_snapshot = create_snapshot();
    _page_snapshot = create_snapshot();
    _buffers_ready = _root_snapshot != nullptr && _page_snapshot != nullptr;
    if (!_buffers_ready) {
        _root_snapshot.reset();
        _page_snapshot.reset();
    }
    return _buffers_ready;
}

void PageTransition::configureAnimation()
{
    auto& options          = _progress.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = TransitionVisualDuration;
    options.bounce         = TransitionBounce;
    options.restDelta      = 0.002F;
    options.restSpeed      = 0.02F;
    _progress.delay        = 0.0F;
    _progress.teleport(0.0F);
    _progress         = 1.0F;
    _last_render_time = 0;
    _active           = true;
}

void PageTransition::compose(float progress)
{
    auto& canvas     = GetHAL().gfxCanvas();
    auto* output     = static_cast<std::uint16_t*>(canvas.getBuffer());
    const auto* root = static_cast<const std::uint16_t*>(_root_snapshot->getBuffer());
    const auto* page = static_cast<const std::uint16_t*>(_page_snapshot->getBuffer());

    const int panel_x         = _direction == Direction::Push ? ScreenWidth - roundToInt(PanelWidth * progress)
                                                              : -PanelExtra + roundToInt(PanelWidth * progress);
    const int page_x          = _direction == Direction::Push ? roundToInt(ScreenWidth * (1.0F - progress))
                                                              : roundToInt(ScreenWidth * progress);
    const float root_progress = std::clamp(progress, 0.0F, 1.0F);
    const int root_x          = _direction == Direction::Push ? -roundToInt(RootTravel * root_progress)
                                                              : -roundToInt(RootTravel * (1.0F - root_progress));
    const int root_source_x   = std::max(0, -root_x);
    const int root_width      = ScreenWidth - root_source_x;

    for (int y = 0; y < ScreenHeight; ++y) {
        auto* output_row     = output + y * ScreenWidth;
        const auto* root_row = root + y * ScreenWidth;
        if (root_width > 0) {
            std::memcpy(output_row, root_row + root_source_x,
                        static_cast<std::size_t>(root_width) * sizeof(std::uint16_t));
        }
    }

    canvas.fillRoundRect(panel_x, 0, PanelWidth, ScreenHeight, PanelRadius, _page_background_color);
    const int panel_right = panel_x + PanelWidth;
    if (_direction == Direction::Push && panel_right < ScreenWidth) {
        const int fill_x = std::max(0, panel_right);
        canvas.fillRect(fill_x, 0, ScreenWidth - fill_x, ScreenHeight, _page_background_color);
    }

    if (_page_surface != TransitionSurface::Snapshot) {
        return;
    }

    const int page_source_x = std::max(0, -page_x);
    const int page_target_x = std::max(0, page_x);
    const int page_width =
        std::clamp(std::min(ScreenWidth - page_source_x, ScreenWidth - page_target_x), 0, ScreenWidth);
    for (int y = 0; y < ScreenHeight; ++y) {
        auto* output_row     = output + y * ScreenWidth;
        const auto* page_row = page + y * ScreenWidth;
        if (page_width > 0) {
            std::memcpy(output_row + page_target_x, page_row + page_source_x,
                        static_cast<std::size_t>(page_width) * sizeof(std::uint16_t));
        }
    }
}

void PageTransition::copyCanvasTo(lgfx::LGFX_Sprite& snapshot)
{
    constexpr std::size_t FrameBytes = ScreenWidth * ScreenHeight * sizeof(std::uint16_t);
    std::memcpy(snapshot.getBuffer(), GetHAL().gfxCanvas().getBuffer(), FrameBytes);
}

}  // namespace settings::view
