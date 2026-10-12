/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

// Define this at compile time (or change it temporarily while profiling).
// When disabled, the monitor has no state and its inline methods are no-ops.
#ifndef CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
#define CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG 0
#endif

#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
#include <atomic>
#include <mooncake_log.h>
#endif

#include <cstdint>

namespace display_debug {

class DisplayFpsMonitor final {
public:
    void recordFrame(bool lvgl)
    {
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
        if (lvgl) {
            _lvgl_frames.fetch_add(1, std::memory_order_relaxed);
        } else {
            _gfx_frames.fetch_add(1, std::memory_order_relaxed);
        }
#else
        (void)lvgl;
#endif
    }

    void update(bool lvgl, std::uint32_t now)
    {
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
        if (!_initialized || _lvgl != lvgl) {
            clearCounters();
            _lvgl         = lvgl;
            _window_start = now;
            _initialized  = true;
            return;
        }

        const std::uint32_t elapsed = now - _window_start;
        if (elapsed < 1000) {
            return;
        }

        const std::uint32_t frames = lvgl ? _lvgl_frames.exchange(0, std::memory_order_relaxed)
                                          : _gfx_frames.exchange(0, std::memory_order_relaxed);
        clearInactiveCounter(lvgl);

        const std::uint32_t fps = elapsed == 0 ? 0 : (frames * 1000U + elapsed / 2U) / elapsed;
        mclog::tagInfo("FPS", "display fps [{}]: {}", lvgl ? "LVGL" : "GFX", fps);
        _window_start = now;
#else
        (void)lvgl;
        (void)now;
#endif
    }

private:
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    void clearCounters()
    {
        _gfx_frames.store(0, std::memory_order_relaxed);
        _lvgl_frames.store(0, std::memory_order_relaxed);
    }

    void clearInactiveCounter(bool lvgl)
    {
        if (lvgl) {
            _gfx_frames.store(0, std::memory_order_relaxed);
        } else {
            _lvgl_frames.store(0, std::memory_order_relaxed);
        }
    }

    std::atomic<std::uint32_t> _gfx_frames{0};
    std::atomic<std::uint32_t> _lvgl_frames{0};
    std::uint32_t _window_start = 0;
    bool _lvgl                  = false;
    bool _initialized           = false;
#endif
};

}  // namespace display_debug
