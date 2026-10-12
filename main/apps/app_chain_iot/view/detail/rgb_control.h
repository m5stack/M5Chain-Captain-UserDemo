/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../menu/chain_node.h"

#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

namespace chain_iot::view {

class RgbControl final {
public:
    enum class Palette {
        Rgb,
        Grayscale,
    };

    explicit RgbControl(Palette palette = Palette::Rgb);

    bool setColor(RgbColor color);
    RgbColor color() const;
    bool handleInput(input::KeyEvent event);
    bool update(std::uint32_t currentTime);
    float expansion() const;
    void draw(lgfx::LGFX_Sprite& canvas, const Frame& collapsed, const Frame& expanded) const;

private:
    Palette _palette = Palette::Rgb;
    RgbColor _color;
    RgbColor _snapshot;
    uitk::AnimateValue _edit_mix;
    uitk::AnimateValue _position;
    float _rendered_mix      = 0.0F;
    float _rendered_position = 0.0F;
    int _selected_index      = 0;
    int _snapshot_index      = 0;
    int _held_edge_direction = 0;
    bool _editing            = false;
    bool _ok_armed           = false;
    bool _dirty              = true;

    void beginEdit();
    void finishEdit(bool cancel);
    void adjust(int direction);
};

}  // namespace chain_iot::view
