/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <M5GFX.h>
#include <cstdint>
#include <hal/hal.h>

namespace view {

inline constexpr int LevelScreenSize                = 240;
inline constexpr float ModeTransitionVisualDuration = 0.42F;
inline constexpr float ModeTransitionBounce         = 0.16F;

struct LevelTransitionOffset {
    float x = 0.0F;
    float y = 0.0F;
};

struct LevelSurfaceTransition {
    float orientation       = 0.0F;
    float lockedOrientation = 0.0F;
    float offset            = 0.0F;
    std::uint32_t color     = 0;
};

LevelTransitionOffset levelTransitionOffset(float distance, float lockedOrientation);
void drawLevelSurface(lgfx::LGFX_Sprite& canvas, float orientation, const LevelTransitionOffset& offset,
                      std::uint32_t color);

class LevelModeView {
public:
    virtual ~LevelModeView() = default;

    virtual void init()                             = 0;
    virtual void update(std::uint32_t currentTime)  = 0;
    virtual void draw(lgfx::LGFX_Sprite& canvas)    = 0;
    virtual void setImuData(const ImuData& data)    = 0;
    virtual void setDataLabelsVisible(bool visible) = 0;
    virtual bool renderRequested() const            = 0;
    virtual void clearRenderRequest()               = 0;
};

}  // namespace view
