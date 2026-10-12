/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "screensaver.h"
#include "effects/cat_screensaver.h"
#include "effects/rps_screensaver.h"
#include "effects/starry_night_screensaver.h"

#include <array>
#include <tools/random/random.hpp>

namespace screensaver {

const std::vector<Registration>& registry()
{
    static const std::vector<Registration> registrations = {
        {"off", "Off", nullptr},
        {"rps", "RPS",
         []() -> std::unique_ptr<Screensaver> { return std::make_unique<RpsScreensaver>(); }},
        {"starry-night", "Starry Night",
         []() -> std::unique_ptr<Screensaver> { return std::make_unique<StarryNightScreensaver>(); }},
        {"cat", "Cat Autumn", []() -> std::unique_ptr<Screensaver> { return std::make_unique<CatScreensaver>(); }},
        {"cat-spring", "Cat Spring",
         []() -> std::unique_ptr<Screensaver> { return std::make_unique<CatScreensaver>(CatGarden::Season::Spring); }},
        {"cat-summer", "Cat Summer",
         []() -> std::unique_ptr<Screensaver> { return std::make_unique<CatScreensaver>(CatGarden::Season::Summer); }},
        {"cat-winter", "Cat Winter",
         []() -> std::unique_ptr<Screensaver> { return std::make_unique<CatScreensaver>(CatGarden::Season::Winter); }},
        {"cat-random", "Cat Random",
         []() -> std::unique_ptr<Screensaver> {
             // The factory runs on every activation, so each visit rolls a fresh season.
             static constexpr std::array<CatGarden::Season, 4> Seasons = {CatGarden::Season::Autumn,
                                                                          CatGarden::Season::Spring,
                                                                          CatGarden::Season::Summer,
                                                                          CatGarden::Season::Winter};
             return std::make_unique<CatScreensaver>(Seasons[smooth_ui_toolkit::Random::getInstance().getInt(0, 3)]);
         }},
    };
    return registrations;
}

const Registration& find(std::string_view id)
{
    for (const auto& registration : registry()) {
        if (registration.id == id) {
            return registration;
        }
    }
    // During simulator development, the newest registered effect is the preview default.
    return registry().back();
}

}  // namespace screensaver
