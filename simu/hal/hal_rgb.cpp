/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

bool Hal::setRgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    _rgb_color = {.red = red, .green = green, .blue = blue};
    return true;
}
