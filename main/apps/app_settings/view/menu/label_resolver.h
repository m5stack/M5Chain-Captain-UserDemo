/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <M5GFX.h>

#include <array>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

namespace settings::view {

struct LabelFontOption {
    std::uint8_t size          = 0;
    const lgfx::LVGLFont* font = nullptr;
};

struct ResolvedLabel {
    std::uint8_t fontSize  = 0;
    std::uint8_t lineCount = 1;
    std::array<std::string, 2> lines;
};

inline std::string normalizeLabel(std::string_view label)
{
    std::string normalized;
    normalized.reserve(label.size());
    bool pending_space = false;
    for (const char character : label) {
        if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
            pending_space = !normalized.empty();
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(character);
    }
    return normalized;
}

inline bool wrapLabel(lgfx::LGFX_Sprite& canvas, const std::string& label, const lgfx::LVGLFont* font,
                      int availableWidth, std::array<std::string, 2>& lines)
{
    if (canvas.textWidth(label.c_str(), font) <= availableWidth) {
        lines = {label, {}};
        return true;
    }

    std::size_t best_break = std::string::npos;
    int best_balance       = availableWidth * 2;
    for (std::size_t offset = label.find(' '); offset != std::string::npos; offset = label.find(' ', offset + 1)) {
        const std::string first  = label.substr(0, offset);
        const std::string second = label.substr(offset + 1);
        const int first_width    = canvas.textWidth(first.c_str(), font);
        const int second_width   = canvas.textWidth(second.c_str(), font);
        if (first_width <= availableWidth && second_width <= availableWidth) {
            const int balance = std::abs(first_width - second_width);
            if (balance < best_balance) {
                best_balance = balance;
                best_break   = offset;
            }
        }
    }
    if (best_break == std::string::npos) {
        return false;
    }

    lines = {label.substr(0, best_break), label.substr(best_break + 1)};
    return true;
}

// Byte offsets always land at UTF-8 character boundaries.
inline std::size_t nextLabelCharacter(const std::string& text, std::size_t offset)
{
    ++offset;
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U) ++offset;
    return offset;
}

inline std::size_t fittingLabelPrefix(lgfx::LGFX_Sprite& canvas, const std::string& text, const lgfx::LVGLFont* font,
                                      int width, const std::string& suffix = {})
{
    std::size_t fit = 0;
    for (std::size_t end = 0; end < text.size();) {
        end = nextLabelCharacter(text, end);
        if (canvas.textWidth((text.substr(0, end) + suffix).c_str(), font) > width) break;
        fit = end;
    }
    return fit;
}

// Prefer the primary font on one line, then smaller balanced layouts. If no
// complete layout fits, wrap at the last font and ellipsize the second line.
inline ResolvedLabel resolveLabel(lgfx::LGFX_Sprite& canvas, std::string_view label, int availableWidth,
                                  std::initializer_list<LabelFontOption> fonts)
{
    canvas.setTextSize(1);
    const std::string normalized = normalizeLabel(label);
    if (fonts.size() == 0 || availableWidth <= 0) {
        return {};
    }

    const auto& primary = *fonts.begin();
    if (canvas.textWidth(normalized.c_str(), primary.font) <= availableWidth) {
        return {primary.size, 1, {normalized, {}}};
    }

    for (auto candidate = fonts.begin() + 1; candidate != fonts.end(); ++candidate) {
        ResolvedLabel resolved;
        resolved.fontSize = candidate->size;
        if (wrapLabel(canvas, normalized, candidate->font, availableWidth, resolved.lines)) {
            resolved.lineCount = resolved.lines[1].empty() ? 1 : 2;
            return resolved;
        }
    }

    const auto& fallback = *(fonts.end() - 1);
    ResolvedLabel resolved;
    resolved.fontSize = fallback.size;
    std::size_t split = fittingLabelPrefix(canvas, normalized, fallback.font, availableWidth);
    const auto space  = normalized.rfind(' ', split);
    if (space != std::string::npos && space > 0) split = space;
    resolved.lines[0]      = normalized.substr(0, split);
    const std::string rest = normalizeLabel(normalized.substr(split));
    if (canvas.textWidth(rest.c_str(), fallback.font) <= availableWidth) {
        resolved.lines[1] = rest;
    } else {
        std::string suffix = "...";
        while (!suffix.empty() && canvas.textWidth(suffix.c_str(), fallback.font) > availableWidth) suffix.pop_back();
        resolved.lines[1] =
            rest.substr(0, fittingLabelPrefix(canvas, rest, fallback.font, availableWidth, suffix)) + suffix;
    }
    if (resolved.lines[0].empty()) {
        resolved.lines[0].swap(resolved.lines[1]);
    }
    resolved.lineCount = resolved.lines[1].empty() ? 1 : 2;
    return resolved;
}

}  // namespace settings::view
