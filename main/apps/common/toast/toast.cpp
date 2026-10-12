/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "toast.h"

#include <M5GFX.h>
#include <algorithm>
#include <assets/assets.h>
#include <cmath>
#include <mooncake_log.h>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view Tag = "toast";

constexpr int ScreenWidth          = 240;
constexpr int ToastClosedY         = -100;
constexpr int ToastOpenedY         = 12;
constexpr int ToastClosedWidth     = 108;
constexpr int ToastOpenedWidth     = 216;
constexpr int ToastHeight          = 64;
constexpr int ToastRadius          = 18;
constexpr int BorderWidth          = 2;
constexpr int TextPadding          = 18;
constexpr int TextWidth            = ToastOpenedWidth - (BorderWidth + TextPadding) * 2;
constexpr int TextLineGap          = 1;
constexpr int BackdropFadeTail     = 36;
constexpr int BackdropClearTail    = 8;
constexpr int BackdropOpenedBottom = ToastOpenedY + ToastHeight + BackdropFadeTail;

constexpr std::uint8_t BackdropTopOpacity = 255;

constexpr float AnimationVisualDuration = 0.4F;
constexpr float AnimationBounce         = 0.3F;
constexpr float AnimationDifference     = 0.0005F;
constexpr float BackdropCloseDuration   = 0.22F;

const lgfx::LVGLFont Font20{&font_noto_sans_semibold_20};
const lgfx::LVGLFont Font16{&font_noto_sans_semibold_16};
const lgfx::LVGLFont Font14{&font_noto_sans_semibold_14};

struct ToastPalette {
    std::uint32_t background = 0xE6F1FE;
    std::uint32_t border     = 0xAFD1F9;
    std::uint32_t text       = 0x005BC4;
};

struct ResolvedText {
    std::uint8_t font_size  = 20;
    std::uint8_t line_count = 1;
    std::array<std::string, 2> lines;
};

ToastPalette paletteFor(view::ToastType type)
{
    switch (type) {
        case view::ToastType::Warning:
            return {0xFEFCE8, 0xDBD38B, 0xC4841D};
        case view::ToastType::Error:
            return {0xFEE7EF, 0xFCB5CD, 0xC20E4D};
        case view::ToastType::Success:
            return {0xE8FAF0, 0xA2E8C1, 0x12A150};
        case view::ToastType::Orange:
            return {0xFFDCD1, 0xFFBFAA, 0xF84C13};
        case view::ToastType::Gray:
            return {0xFFFFFF, 0xD9D9D9, 0x11181C};
        case view::ToastType::Rose:
            return {0xFFE9F4, 0xFFBFDF, 0xFF339A};
        case view::ToastType::Dark:
            return {0x383838, 0x222222, 0xF4F3F3};
        case view::ToastType::Info:
        default:
            return {};
    }
}

void configureSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = AnimationVisualDuration;
    options.bounce         = AnimationBounce;
    options.restDelta      = 0.01F;
    options.restSpeed      = 0.01F;
    animation.delay        = 0.0F;
}

std::size_t codepointLength(std::string_view text, std::size_t offset)
{
    const auto first   = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
    }

    if (offset + length > text.size()) {
        return 1;
    }
    for (std::size_t index = 1; index < length; ++index) {
        if ((static_cast<unsigned char>(text[offset + index]) & 0xC0U) != 0x80U) {
            return 1;
        }
    }
    return length;
}

std::uint32_t decodeCodepoint(std::string_view text, std::size_t offset, std::size_t* nextOffset = nullptr)
{
    const std::size_t length = codepointLength(text, offset);
    const auto first         = static_cast<unsigned char>(text[offset]);
    std::uint32_t value      = first;
    if (length > 1) {
        value = first & (0x7FU >> length);
        for (std::size_t index = 1; index < length; ++index) {
            value = (value << 6U) | (static_cast<unsigned char>(text[offset + index]) & 0x3FU);
        }
    }
    if (nextOffset != nullptr) {
        *nextOffset = offset + length;
    }
    return value;
}

int measureText(std::string_view text, const lv_font_t* font)
{
    int width          = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::size_t next_offset     = offset;
        const std::uint32_t current = decodeCodepoint(text, offset, &next_offset);
        const std::uint32_t next    = next_offset < text.size() ? decodeCodepoint(text, next_offset) : 0U;
        width += static_cast<int>(lv_font_get_glyph_width(font, current, next));
        offset = next_offset;
    }
    return width;
}

std::vector<std::string> splitCodepoints(std::string_view text)
{
    std::vector<std::string> units;
    units.reserve(text.size());

    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t length  = codepointLength(text, offset);
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        if (first == '\n' || first == '\r' || first == '\t') {
            units.emplace_back(" ");
        } else {
            units.emplace_back(text.substr(offset, length));
        }
        offset += length;
    }
    return units;
}

bool isSpace(const std::string& unit)
{
    return unit.size() == 1 && unit.front() == ' ';
}

void trimSpaces(std::vector<std::string>& units)
{
    while (!units.empty() && isSpace(units.front())) {
        units.erase(units.begin());
    }
    while (!units.empty() && isSpace(units.back())) {
        units.pop_back();
    }
}

std::string joinUnits(const std::vector<std::string>& units, std::size_t begin, std::size_t end)
{
    std::string result;
    for (std::size_t index = begin; index < end; ++index) {
        result += units[index];
    }
    return result;
}

bool wrapText(const std::vector<std::string>& input, const lv_font_t* font, std::array<std::string, 2>& lines,
              std::uint8_t& lineCount)
{
    auto units = input;
    trimSpaces(units);
    lines     = {};
    lineCount = 1;
    if (units.empty()) {
        return true;
    }

    std::size_t start = 0;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        while (start < units.size() && isSpace(units[start])) {
            ++start;
        }
        if (start >= units.size()) {
            lineCount = static_cast<std::uint8_t>(std::max<std::size_t>(1, line_index));
            return true;
        }

        if (measureText(joinUnits(units, start, units.size()), font) <= TextWidth) {
            lines[line_index] = joinUnits(units, start, units.size());
            lineCount         = static_cast<std::uint8_t>(line_index + 1);
            return true;
        }

        std::size_t best_end       = start;
        std::size_t last_space_end = start;
        for (std::size_t end = start + 1; end <= units.size(); ++end) {
            if (measureText(joinUnits(units, start, end), font) > TextWidth) {
                break;
            }
            best_end = end;
            if (isSpace(units[end - 1])) {
                last_space_end = end;
            }
        }
        if (best_end == start) {
            return false;
        }

        if (last_space_end > start) {
            lines[line_index] = joinUnits(units, start, last_space_end - 1);
            start             = last_space_end;
        } else {
            lines[line_index] = joinUnits(units, start, best_end);
            start             = best_end;
        }
        lineCount = static_cast<std::uint8_t>(line_index + 1);
    }

    while (start < units.size() && isSpace(units[start])) {
        ++start;
    }
    return start == units.size();
}

ResolvedText resolveText(std::string_view message)
{
    const auto units            = splitCodepoints(message);
    const std::string full_text = joinUnits(units, 0, units.size());
    if (measureText(full_text, &font_noto_sans_semibold_20) <= TextWidth) {
        return {20, 1, {full_text, {}}};
    }

    ResolvedText resolved;
    for (const auto& [font_size, font] : {std::pair<std::uint8_t, const lv_font_t*>{16, &font_noto_sans_semibold_16},
                                          std::pair<std::uint8_t, const lv_font_t*>{14, &font_noto_sans_semibold_14}}) {
        if (wrapText(units, font, resolved.lines, resolved.line_count)) {
            resolved.font_size = font_size;
            return resolved;
        }
    }

    auto prefix_units = units;
    trimSpaces(prefix_units);
    while (!prefix_units.empty()) {
        while (!prefix_units.empty() && isSpace(prefix_units.back())) {
            prefix_units.pop_back();
        }
        auto candidate = prefix_units;
        candidate.emplace_back("...");
        if (wrapText(candidate, &font_noto_sans_semibold_14, resolved.lines, resolved.line_count)) {
            resolved.font_size = 14;
            return resolved;
        }
        prefix_units.pop_back();
    }

    resolved.font_size  = 14;
    resolved.line_count = 1;
    resolved.lines      = {"...", {}};
    return resolved;
}

const lgfx::LVGLFont* fontFor(std::uint8_t size)
{
    switch (size) {
        case 14:
            return &Font14;
        case 16:
            return &Font16;
        case 20:
        default:
            return &Font20;
    }
}

void setAnimationTarget(uitk::AnimateValue& positionY, uitk::AnimateValue& width, int targetY, int targetWidth,
                        bool teleport)
{
    if (teleport) {
        positionY.teleport(static_cast<float>(targetY));
        width.teleport(static_cast<float>(targetWidth));
        return;
    }
    positionY = static_cast<float>(targetY);
    width     = static_cast<float>(targetWidth);
}

}  // namespace

namespace view {

ToastManager::ToastManager()
{
    auto& options          = _backdrop_bottom.easingOptions();
    options.duration       = AnimationVisualDuration;
    options.easingFunction = uitk::ease::ease_in_out_sine;
    _backdrop_bottom.delay = 0.0F;
    _backdrop_bottom.teleport(0.0F);
}

Toast::Toast(Config config) : _config(std::move(config)), _text_layout(buildTextLayout(_config.message))
{
    configureSpring(_position_y);
    configureSpring(_width);
    setAnimationTarget(_position_y, _width, ToastClosedY, ToastClosedWidth, true);
    _position_y_value = _position_y.directValue();
    _width_value      = _width.directValue();
}

Toast::TextLayout Toast::buildTextLayout(std::string_view message)
{
    const auto resolved = resolveText(message);
    return {resolved.font_size, resolved.line_count, resolved.lines};
}

bool Toast::update(std::uint32_t currentTime)
{
    const float previous_y     = _position_y.directValue();
    const float previous_width = _width.directValue();
    const State previous_state = _state;
    const float current_time   = static_cast<float>(currentTime) / 1000.0F;

    _position_y.update(current_time);
    _width.update(current_time);
    const float current_y     = _position_y.directValue();
    const float current_width = _width.directValue();

    if (_position_y.done() && _width.done()) {
        if (_state == State::Opening) {
            _state     = State::Opened;
            _opened_at = currentTime;
        } else if (_state == State::Closing) {
            _state = State::Closed;
        }
    }

    if (_state == State::Opened && currentTime - _opened_at >= _config.durationMs) {
        close();
    }

    _position_y_value = current_y;
    _width_value      = current_width;
    return previous_state != _state || std::abs(current_y - previous_y) > AnimationDifference ||
           std::abs(current_width - previous_width) > AnimationDifference;
}

void Toast::draw(lgfx::LGFX_Sprite& canvas) const
{
    if (_state == State::Closed) {
        return;
    }

    const int width    = std::max(1, static_cast<int>(std::lround(_width_value)));
    const int x        = (ScreenWidth - width) / 2;
    const int y        = static_cast<int>(std::lround(_position_y_value));
    const auto palette = paletteFor(_config.type);

    canvas.fillSmoothRoundRect(x, y, width, ToastHeight, std::min(ToastRadius, width / 2), palette.border);
    if (width > BorderWidth * 2 && ToastHeight > BorderWidth * 2) {
        canvas.fillSmoothRoundRect(
            x + BorderWidth, y + BorderWidth, width - BorderWidth * 2, ToastHeight - BorderWidth * 2,
            std::min(ToastRadius - BorderWidth, (width - BorderWidth * 2) / 2), palette.background);
    }

    const int content_x     = x + BorderWidth + TextPadding;
    const int content_width = width - (BorderWidth + TextPadding) * 2;
    if (content_width <= 0) {
        return;
    }

    canvas.setClipRect(content_x, y + BorderWidth, content_width, ToastHeight - BorderWidth * 2);
    canvas.setTextSize(1);
    canvas.setTextColor(palette.text);
    canvas.setBaseColor(palette.background);
    const auto* font = fontFor(_text_layout.fontSize);
    canvas.setFont(font);

    const int center_x = x + width / 2;
    if (_text_layout.lineCount <= 1) {
        canvas.setTextDatum(lgfx::textdatum_t::middle_center);
        canvas.drawString(_text_layout.lines[0].c_str(), center_x, y + ToastHeight / 2);
    } else {
        const int line_height = canvas.fontHeight(font);
        const int text_height = line_height * 2 + TextLineGap;
        const int text_y      = y + (ToastHeight - text_height) / 2;
        canvas.setTextDatum(lgfx::textdatum_t::top_center);
        canvas.drawString(_text_layout.lines[0].c_str(), center_x, text_y);
        canvas.drawString(_text_layout.lines[1].c_str(), center_x, text_y + line_height + TextLineGap);
    }
    canvas.setClipRect(0, 0, canvas.width(), canvas.height());
}

void Toast::close(bool teleport)
{
    _state = State::Closing;
    setAnimationTarget(_position_y, _width, ToastClosedY, ToastClosedWidth, teleport);
    if (teleport) {
        _state            = State::Closed;
        _position_y_value = _position_y.directValue();
        _width_value      = _width.directValue();
    }
}

void Toast::open(bool teleport)
{
    _state = State::Opening;
    setAnimationTarget(_position_y, _width, ToastOpenedY, ToastOpenedWidth, teleport);
    if (teleport) {
        _state            = State::Opened;
        _position_y_value = _position_y.directValue();
        _width_value      = _width.directValue();
    }
}

void Toast::stack(bool teleport)
{
    if (_state == State::Closing || _state == State::Closed) {
        return;
    }

    _stack_depth          = std::min(_stack_depth + 1, static_cast<int>(ToastManager::ToastCapacity) - 1);
    const int stack_y     = ToastOpenedY + _stack_depth * (12 - _stack_depth * 2);
    const int stack_width = ToastOpenedWidth - _stack_depth * 40;
    setAnimationTarget(_position_y, _width, stack_y, stack_width, teleport);
}

bool ToastManager::push(std::string_view message, ToastType type, std::uint32_t durationMs)
{
    if (_queue_size >= QueueCapacity) {
        mclog::tagWarn(Tag, "request queue is full: {}", _queue_size);
        return false;
    }

    const std::size_t tail = (_queue_head + _queue_size) % QueueCapacity;
    _queue[tail]           = {type, durationMs, std::string(message)};
    ++_queue_size;
    return true;
}

bool ToastManager::update(std::uint32_t currentTime)
{
    bool changed = false;
    Toast::Config config;
    if (popRequest(config)) {
        for (auto& toast : _toasts) {
            if (toast != nullptr) {
                toast->stack();
            }
        }

        _toasts[_next_slot] = std::make_unique<Toast>(std::move(config));
        _toasts[_next_slot]->open();
        _next_slot = (_next_slot + 1) % ToastCapacity;
        changed    = true;
    }

    for (auto& toast : _toasts) {
        if (toast == nullptr) {
            continue;
        }
        changed = toast->update(currentTime) || changed;
        if (toast->state() == Toast::State::Closed) {
            toast.reset();
            changed = true;
        }
    }

    const bool backdrop_visible = std::any_of(_toasts.begin(), _toasts.end(), [](const auto& toast) {
        return toast != nullptr && (toast->state() == Toast::State::Opening || toast->state() == Toast::State::Opened);
    });
    const float backdrop_target = backdrop_visible ? static_cast<float>(BackdropOpenedBottom) : 0.0F;
    if (_backdrop_bottom.end != backdrop_target) {
        _backdrop_bottom.easingOptions().duration = backdrop_visible ? AnimationVisualDuration : BackdropCloseDuration;
        _backdrop_bottom                          = backdrop_target;
    }
    const float previous_backdrop_bottom = _backdrop_bottom.directValue();
    _backdrop_bottom.update(static_cast<float>(currentTime) / 1000.0F);
    _backdrop_bottom_value = _backdrop_bottom.directValue();
    changed = std::abs(_backdrop_bottom_value - previous_backdrop_bottom) > AnimationDifference || changed;
    return changed;
}

void ToastManager::draw(lgfx::LGFX_Sprite& canvas, std::uint32_t backdropColor, bool backdropEnabled) const
{
    if (backdropEnabled) {
        const int backdrop_bottom =
            std::clamp(static_cast<int>(std::lround(_backdrop_bottom_value)), 0, static_cast<int>(canvas.height()));
        const int solid_bottom = backdrop_bottom / 3;
        const int fade_bottom  = std::max(solid_bottom, backdrop_bottom - BackdropClearTail);
        const int fade_height  = fade_bottom - solid_bottom;
        for (int y = 0; y < backdrop_bottom; ++y) {
            const float progress =
                y < solid_bottom ? 0.0F
                                 : (y >= fade_bottom || fade_height <= 1
                                        ? 1.0F
                                        : static_cast<float>(y - solid_bottom) / static_cast<float>(fade_height - 1));
            const float progress_cubed  = progress * progress * progress;
            const float smooth_progress = progress_cubed * (progress * (progress * 6.0F - 15.0F) + 10.0F);
            const auto opacity          = static_cast<std::uint8_t>(
                std::lround(static_cast<float>(BackdropTopOpacity) * (1.0F - smooth_progress)));
            if (opacity != 0U) {
                canvas.fillRectAlpha(0, y, ScreenWidth, 1, opacity, backdropColor);
            }
        }
    }

    for (int depth = static_cast<int>(ToastCapacity) - 1; depth >= 0; --depth) {
        for (const auto& toast : _toasts) {
            if (toast != nullptr && toast->stackDepth() == depth) {
                toast->draw(canvas);
            }
        }
    }
}

bool ToastManager::dismiss()
{
    bool changed = _queue_size != 0;
    for (auto& request : _queue) {
        request = {};
    }
    _queue_head = 0;
    _queue_size = 0;

    for (auto& toast : _toasts) {
        if (toast == nullptr || toast->state() == Toast::State::Closed || toast->state() == Toast::State::Closing) {
            continue;
        }
        toast->close();
        changed = true;
    }
    return changed;
}

void ToastManager::clear()
{
    for (auto& toast : _toasts) {
        toast.reset();
    }
    for (auto& request : _queue) {
        request = {};
    }
    _queue_head = 0;
    _queue_size = 0;
    _next_slot  = 0;
    _backdrop_bottom.teleport(0.0F);
    _backdrop_bottom_value = 0.0F;
}

bool ToastManager::active() const
{
    return std::any_of(_toasts.begin(), _toasts.end(),
                       [](const auto& toast) { return toast != nullptr && toast->state() != Toast::State::Closed; });
}

bool ToastManager::popRequest(Toast::Config& config)
{
    if (_queue_size == 0) {
        return false;
    }

    config              = std::move(_queue[_queue_head]);
    _queue[_queue_head] = {};
    _queue_head         = (_queue_head + 1) % QueueCapacity;
    --_queue_size;
    return true;
}

ToastManager& getToastManager()
{
    static ToastManager manager;
    return manager;
}

void pop_a_toast(std::string_view message, ToastType type, std::uint32_t durationMs)
{
    getToastManager().push(message, type, durationMs);
}

}  // namespace view
