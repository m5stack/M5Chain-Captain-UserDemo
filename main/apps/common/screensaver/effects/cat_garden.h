#pragma once

#include <cstdio>
#include <M5GFX.h>
#include <array>
#include <cstdint>
#include <vector>

namespace screensaver {

// An 80 x 80 pixel painting, displayed on the same 3x grid as the cat.
class CatGarden {
public:
    enum class Season { Autumn, Spring, Summer, Winter };
    explicit CatGarden(Season season = Season::Autumn) : _season(season)
    {
    }
    Season season() const
    {
        return _season;
    }
    void init(std::uint32_t now);
    bool update(std::uint32_t now);
    void draw(lgfx::LGFX_Sprite& canvas) const;
    void drawDust(lgfx::LGFX_Sprite& canvas, bool foreground) const;

private:
    Season _season;
    std::uint32_t color(std::uint32_t autumn) const;
    struct Dust {
        float x, y, age, life, speed, phase;
        int draw_x = 0;
        int draw_y = 0;
        int alpha  = 0;
    };
    void resetDust(Dust& dust, bool stagger);
    std::array<Dust, 16> _dust{};
    std::uint32_t _last_dust_tick = 0;
    struct Plant {
        int x;
        int y;
        bool flower;
        bool gold;
        std::uint32_t phase;
        std::uint32_t period;
        int sway             = 0;
        std::uint32_t petals = 0xD9CF9E;
        std::uint32_t center = 0xB89A59;
    };
    std::vector<Plant> _plants;
    void rect(int x, int y, int w, int h, std::uint32_t color);
    std::array<std::uint16_t, 80 * 80> _pixels{};
};

}  // namespace screensaver
