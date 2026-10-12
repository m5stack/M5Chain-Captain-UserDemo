/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <smooth_ui_toolkit.hpp>
#include <string>
#include <string_view>
#include <uitk/short_namespace.hpp>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace view {

enum class ToastType : std::uint8_t {
    Info = 0,
    Warning,
    Error,
    Success,
    Orange,
    Gray,
    Rose,
    Dark,
};

class Toast {
public:
    enum class State : std::uint8_t {
        Closed,
        Opening,
        Opened,
        Closing,
    };

    struct Config {
        ToastType type           = ToastType::Info;
        std::uint32_t durationMs = 1600;
        std::string message;
    };

    explicit Toast(Config config);

    bool update(std::uint32_t currentTime);
    void draw(lgfx::LGFX_Sprite& canvas) const;

    void close(bool teleport = false);
    void open(bool teleport = false);
    void stack(bool teleport = false);

    State state() const
    {
        return _state;
    }

    int stackDepth() const
    {
        return _stack_depth;
    }

private:
    struct TextLayout {
        std::uint8_t fontSize  = 20;
        std::uint8_t lineCount = 1;
        std::array<std::string, 2> lines;
    };

    static TextLayout buildTextLayout(std::string_view message);

    Config _config;
    TextLayout _text_layout;
    uitk::AnimateValue _position_y;
    uitk::AnimateValue _width;
    float _position_y_value  = 0.0F;
    float _width_value       = 0.0F;
    State _state             = State::Closed;
    int _stack_depth         = 0;
    std::uint32_t _opened_at = 0;
};

class ToastManager {
public:
    static constexpr std::size_t QueueCapacity = 5;
    static constexpr std::size_t ToastCapacity = 3;

    ToastManager();

    bool push(std::string_view message, ToastType type = ToastType::Info, std::uint32_t durationMs = 1600);
    bool update(std::uint32_t currentTime);
    void draw(lgfx::LGFX_Sprite& canvas, std::uint32_t backdropColor = 0xFFFFFF, bool backdropEnabled = true) const;

    // Discard queued requests and animate every visible toast closed.
    bool dismiss();
    void clear();
    bool active() const;
    bool hasPending() const
    {
        return _queue_size != 0;
    }

private:
    bool popRequest(Toast::Config& config);

    std::array<Toast::Config, QueueCapacity> _queue;
    std::array<std::unique_ptr<Toast>, ToastCapacity> _toasts;
    std::size_t _queue_head = 0;
    std::size_t _queue_size = 0;
    std::size_t _next_slot  = 0;
    uitk::AnimateValue _backdrop_bottom;
    float _backdrop_bottom_value = 0.0F;
};

ToastManager& getToastManager();

// Queue a notification for the active GFX page. The host owns update/draw timing.
void pop_a_toast(std::string_view message, ToastType type = ToastType::Info, std::uint32_t durationMs = 1600);

}  // namespace view
