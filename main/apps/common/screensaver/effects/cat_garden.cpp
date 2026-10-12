#include "cat_garden.h"

#include <algorithm>
#include <cmath>
#include <tools/random/random.hpp>

namespace {

// Keep the same composition and animation; only the seasonal palette changes.
std::uint32_t springColor(std::uint32_t autumn)
{
    switch (autumn) {
        case 0xA4A373:
            return 0xA8BC8D;  // meadow
        case 0x999D6C:
            return 0xA2B687;  // shaded grass
        case 0xB1AB79:
            return 0xAEBF93;  // sunlit grass
        case 0x909765:
            return 0x8CA477;
        case 0xBDB17B:
            return 0xC3D2A5;
        case 0x858D5C:
            return 0x799669;
        case 0xC6B982:
            return 0xCDDCB1;
        case 0xB7A672:
            return 0xACC393;
        case 0xBC985D:
            return 0xDEA3B4;  // fallen pink petals
        case 0xAF8056:
            return 0xCF91A5;
        case 0xC6A36A:
            return 0xF0C0CA;
        case 0xBB9161:
            return 0xE8B2C0;
        case 0x8A8058:
            return 0xAC8792;
        case 0x87876C:
            return 0x89918B;  // cooler stones
        case 0xAEAA90:
            return 0xB0BAB0;
        case 0xC2BBA0:
            return 0xC9CEC2;
        case 0x7E8C5E:
            return 0x759465;
        case 0xD9CF9E:
            return 0xF3BCCD;  // blossoms
        case 0xB89A59:
            return 0xCB839F;
        case 0xFFF0C2:
            return 0xFFF0EE;  // rosy-white light motes
        default:
            return autumn;
    }
}

std::uint32_t summerColor(std::uint32_t autumn)
{
    switch (autumn) {
        case 0xA4A373:
            return 0x7FA254;  // rich grass
        case 0x999D6C:
            return 0x76994C;
        case 0xB1AB79:
            return 0x8FB25E;
        case 0x909765:
            return 0x6B9144;
        case 0xBDB17B:
            return 0x9FC070;
        case 0x858D5C:
            return 0x5F873F;
        case 0xC6B982:
            return 0xA9CA7C;
        case 0xB7A672:
            return 0x95BB66;
        case 0xBC985D:
            return 0xF4F5EE;  // daisy petals
        case 0xAF8056:
            return 0xEEF0E6;
        case 0xC6A36A:
            return 0xF7D98A;  // daisy centres
        case 0xBB9161:
            return 0xF2D27E;
        case 0x8A8058:
            return 0xA8B36B;  // green stems
        case 0x87876C:
            return 0x87917C;
        case 0xAEAA90:
            return 0xAAB49E;
        case 0xC2BBA0:
            return 0xC3CCB8;
        case 0x7E8C5E:
            return 0x5F873F;
        case 0xD9CF9E:
            return 0xF6F7F1;  // white petals
        case 0xB89A59:
            return 0xE8B84E;  // yellow centres
        case 0xFFF0C2:
            return 0xFFF6D9;  // warm sun motes
        default:
            return autumn;
    }
}

std::uint32_t winterColor(std::uint32_t autumn)
{
    switch (autumn) {
        case 0xA4A373:
            return 0xE2EBF2;  // snow
        case 0x999D6C:
            return 0xD6E2EC;  // shaded snow
        case 0xB1AB79:
            return 0xECF2F6;  // sunlit snow
        case 0x909765:
            return 0xCBDCE8;
        case 0xBDB17B:
            return 0xF1F5F8;
        case 0x858D5C:
            return 0xC1D6E3;
        case 0xC6B982:
            return 0xE8F0F5;
        case 0xB7A672:
            return 0xDDE9F0;
        case 0xBC985D:
            return 0xF4F8FB;  // snow mounds
        case 0xAF8056:
            return 0xEDF3F8;
        case 0xC6A36A:
            return 0xDFEAF2;
        case 0xBB9161:
            return 0xE7EFF5;
        case 0x8A8058:
            return 0x8E8370;  // bare twigs
        case 0x87876C:
            return 0x8C98A4;  // cold stones
        case 0xAEAA90:
            return 0xBDC7D0;
        case 0xC2BBA0:
            return 0xD3DAE2;
        case 0x7E8C5E:
            return 0x9FB4A0;  // snowdrop stems
        case 0xD9CF9E:
            return 0xF2F6F2;  // snowdrop petals
        case 0xB89A59:
            return 0xE2DC9C;  // snowdrop centres
        case 0xFFF0C2:
            return 0xFFFFFF;  // snow motes
        default:
            return autumn;
    }
}

}  // namespace

namespace screensaver {

std::uint32_t CatGarden::color(std::uint32_t autumn) const
{
    switch (_season) {
        case Season::Spring:
            return springColor(autumn);
        case Season::Summer:
            return summerColor(autumn);
        case Season::Winter:
            return winterColor(autumn);
        default:
            return autumn;
    }
}

void CatGarden::rect(int x, int y, int w, int h, std::uint32_t color)
{
    color            = this->color(color);
    const auto pixel = lgfx::color565((color >> 16) & 255, (color >> 8) & 255, color & 255);
    for (int py = std::max(0, y); py < std::min(80, y + h); ++py) {
        for (int px = std::max(0, x); px < std::min(80, x + w); ++px) {
            _pixels[py * 80 + px] = pixel;
        }
    }
}

void CatGarden::init(std::uint32_t now)
{
    _last_dust_tick = now;
    for (auto& dust : _dust) resetDust(dust, true);
    _plants.clear();
    _plants.reserve(48);
    auto& rng         = smooth_ui_toolkit::Random::getInstance();
    const auto random = [&rng](int n) { return rng.getInt(0, n - 1); };

    // Smooth, irregular patches of the same ground palette. Generate once per
    // visit; broad variation dominates and smaller variation breaks up edges.
    std::array<float, 36> broad;
    std::array<float, 121> detail;
    for (auto& value : broad) value = rng.getFloat(0.0F, 1.0F);
    for (auto& value : detail) value = rng.getFloat(0.0F, 1.0F);
    const auto sample = [](const auto& field, int stride, int spacing, int x, int y) {
        const int gx = x / spacing, gy = y / spacing;
        float tx           = static_cast<float>(x % spacing) / spacing;
        float ty           = static_cast<float>(y % spacing) / spacing;
        tx                 = tx * tx * (3.0F - 2.0F * tx);
        ty                 = ty * ty * (3.0F - 2.0F * ty);
        const float top    = field[gy * stride + gx] * (1.0F - tx) + field[gy * stride + gx + 1] * tx;
        const float bottom = field[(gy + 1) * stride + gx] * (1.0F - tx) + field[(gy + 1) * stride + gx + 1] * tx;
        return top * (1.0F - ty) + bottom * ty;
    };
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 80; ++x) {
            const float value         = sample(broad, 6, 16, x, y) * 0.85F + sample(detail, 11, 8, x, y) * 0.15F;
            const std::uint32_t color = value < 0.30F ? 0x999D6CU : (value > 0.69F ? 0xB1AB79U : 0xA4A373U);
            rect(x, y, 1, 1, color);
        }
    }
    for (int i = 0; i < 125; ++i) {
        const int x = 2 + random(76);
        const int y = 2 + random(76);
        if (x > 18 && x < 62 && y > 18 && y < 62 && i % 5 != 0) {
            continue;
        }
        const bool gold = random(3) == 0;
        rect(x, y, 2, 1, gold ? 0xBDB17B : 0x909765);
        if (i % 3 == 0) {
            _plants.push_back({x, y, false, gold, static_cast<std::uint32_t>(random(6000)),
                               static_cast<std::uint32_t>(4000 + random(2500))});
        }
    }

    // Small scattered autumn leaves, with a darker stem rather than outlines.
    constexpr int leaves[][2] = {{10, 18}, {15, 20}, {63, 9},  {68, 12}, {71, 39}, {8, 52},
                                 {24, 69}, {29, 72}, {46, 11}, {49, 48}, {73, 74}};
    for (unsigned i = 0; i < sizeof(leaves) / sizeof(leaves[0]); ++i) {
        const int x = leaves[i][0] + random(7) - 3, y = leaves[i][1] + random(7) - 3;
        rect(x, y, 3, 1, i % 2 ? 0xBC985D : 0xAF8056);
        rect(x + 1, y - 1, 2, 1, i % 2 ? 0xC6A36A : 0xBB9161);
        rect(x - 1, y + 1, 1, 1, 0x8A8058);
    }

    constexpr int stones[][2] = {{8, 32}, {12, 34}, {55, 15}, {58, 16}, {37, 73}, {72, 49}};
    for (const auto& anchor : stones) {
        const std::array<int, 2> p = {anchor[0] + random(7) - 3, anchor[1] + random(7) - 3};
        rect(p[0], p[1] + 1, 3, 1, 0x87876C);
        rect(p[0], p[1], 3, 1, 0xAEAA90);
        rect(p[0] + 1, p[1] - 1, 2, 1, 0xC2BBA0);
    }
    constexpr std::uint32_t Petals[]        = {0xF3BCCD, 0xF4ECD5, 0xEDDB9B, 0xC9B9E4};
    constexpr std::uint32_t Centers[]       = {0xCB839F, 0xD5B267, 0xC29B59, 0x9882B6};
    constexpr std::uint32_t SummerPetals[]  = {0xF6F7F1, 0xEBDC6E, 0xD6544A, 0xC25C8E};
    constexpr std::uint32_t SummerCenters[] = {0xE8B84E, 0xC89B3C, 0x63251F, 0x8C2F6A};
    int flower_index                        = random(4);
    const auto addFlower                    = [&](int x, int y) {
        x += random(7) - 3;
        y += random(7) - 3;
        rect(x, y + 1, 1, 3, 0x7E8C5E);
        Plant flower{x,
                     y,
                     true,
                     false,
                     static_cast<std::uint32_t>(random(6000)),
                     static_cast<std::uint32_t>(4500 + random(2500))};
        if (_season == Season::Spring || _season == Season::Summer) {
            const int variant = flower_index++ % 4;
            const bool spring = _season == Season::Spring;
            flower.petals     = spring ? Petals[variant] : SummerPetals[variant];
            flower.center     = spring ? Centers[variant] : SummerCenters[variant];
        }
        _plants.push_back(flower);
    };
    constexpr int flowers[][2] = {{6, 9}, {10, 11}, {7, 13}, {69, 24}, {73, 27}, {17, 64}};
    for (const auto& anchor : flowers) addFlower(anchor[0], anchor[1]);
    if (_season == Season::Spring || _season == Season::Summer) {
        constexpr int wildflowers[][2] = {{27, 9}, {51, 15}, {6, 42}, {71, 55}, {38, 72}, {57, 69}};
        for (const auto& anchor : wildflowers) addFlower(anchor[0], anchor[1]);
    } else if (_season == Season::Winter) {
        constexpr int snowdrops[][2] = {{28, 10}, {52, 16}, {7, 43}, {70, 56}};
        for (const auto& anchor : snowdrops) addFlower(anchor[0], anchor[1]);
    }
    update(now);
}

void CatGarden::resetDust(Dust& dust, bool stagger)
{
    auto& random    = smooth_ui_toolkit::Random::getInstance();
    const bool snow = _season == Season::Winter;
    dust.x          = random.getFloat(8.0F, 224.0F);
    dust.y          = snow ? random.getFloat(0.0F, 130.0F) : random.getFloat(45.0F, 235.0F);
    dust.life       = snow ? random.getFloat(7.0F, 12.0F) : random.getFloat(9.0F, 16.0F);
    dust.age        = stagger ? random.getFloat(0.0F, dust.life) : 0.0F;
    dust.speed      = snow ? random.getFloat(7.0F, 14.0F) : random.getFloat(2.0F, 4.0F);
    dust.phase      = random.getFloat(0.0F, 6.2831853F);
}

bool CatGarden::update(std::uint32_t now)
{
    constexpr int Sway[] = {0, 0, 1, 1, 0, 0, -1, 0};
    bool changed         = false;
    for (auto& plant : _plants) {
        const int sway = Sway[((now % plant.period + plant.phase) % plant.period) * 8 / plant.period];
        changed |= sway != plant.sway;
        plant.sway = sway;
    }
    const float dt = std::min(now - _last_dust_tick, std::uint32_t{100}) / 1000.0F;
    // Winter motes are snowflakes and fall; the other seasons' dust rises.
    const float fall = _season == Season::Winter ? 1.0F : -1.0F;
    _last_dust_tick  = now;
    for (auto& dust : _dust) {
        dust.age += dt;
        if (dust.age >= dust.life) resetDust(dust, false);
        const float envelope = std::sin(3.14159265F * dust.age / dust.life);
        const int alpha      = static_cast<int>(envelope * envelope * 216.0F) / 8 * 8;
        const int x =
            static_cast<int>(std::lround(dust.x + dust.age * 0.7F + std::sin(dust.age * 0.7F + dust.phase) * 3.0F));
        const int y = static_cast<int>(std::lround(dust.y + fall * dust.age * dust.speed));
        changed |= x != dust.draw_x || y != dust.draw_y || alpha != dust.alpha;
        dust.draw_x = x;
        dust.draw_y = y;
        dust.alpha  = alpha;
    }
    return changed;
}

void CatGarden::draw(lgfx::LGFX_Sprite& canvas) const
{
    // Merge neighboring equal pixels; most of the image is large flat areas.
    for (int y = 0; y < 80; ++y) {
        for (int x = 0; x < 80;) {
            const auto color = _pixels[y * 80 + x];
            int end          = x + 1;
            while (end < 80 && _pixels[y * 80 + end] == color) {
                ++end;
            }
            canvas.fillRect(x * 3, y * 3, (end - x) * 3, 3, color);
            x = end;
        }
    }
    const auto pixelRect = [this, &canvas](int x, int y, int w, int h, std::uint32_t color) {
        canvas.fillRect(x * 3, y * 3, w * 3, h * 3, this->color(color));
    };
    for (const auto& plant : _plants) {
        const int x = plant.x, y = plant.y, tip = x + plant.sway;
        if (plant.flower) {
            pixelRect(tip, y + 1, 1, 1, 0x7E8C5E);
            pixelRect(tip - 1, y, 3, 1, plant.petals);
            pixelRect(tip, y - 1, 1, 3, plant.petals);
            pixelRect(tip, y, 1, 1, plant.center);
        } else {
            pixelRect(x, y - 1, 1, 1, plant.gold ? 0xC6B982 : 0x858D5C);
            pixelRect(tip, y - 2, 1, 1, plant.gold ? 0xC6B982 : 0x858D5C);
            pixelRect(x + 2, y, 1, 1, plant.gold ? 0xB7A672 : 0x858D5C);
            pixelRect(tip + 2, y - 1, 1, 1, plant.gold ? 0xB7A672 : 0x858D5C);
        }
    }
    drawDust(canvas, false);
}

void CatGarden::drawDust(lgfx::LGFX_Sprite& canvas, bool foreground) const
{
    // Small distant motes behind the cat, a few larger ones in front.
    for (std::size_t i = 0; i < _dust.size(); ++i) {
        const bool near = i % 3 == 0;
        if (near != foreground) continue;
        const auto& dust = _dust[i];
        const int size   = near ? 6 : 4;
        const int alpha  = near ? dust.alpha : dust.alpha * 3 / 4;
        if (alpha > 0) {
            canvas.fillRectAlpha(dust.draw_x, dust.draw_y, size, size, alpha, color(0xFFF0C2U));
        }
    }
}

}  // namespace screensaver
