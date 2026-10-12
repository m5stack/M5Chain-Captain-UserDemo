/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace settings::view {

struct Frame {
    int x      = 0;
    int y      = 0;
    int width  = 0;
    int height = 0;
};

struct Theme {
    std::uint32_t background       = 0xFFFFFF;
    std::uint32_t surface          = 0xE9DAF8;
    std::uint32_t surfaceEditing   = 0x9E7FBA;
    std::uint32_t text             = 0x6B496C;
    std::uint32_t textEditing      = 0xFFFFFF;
    std::uint32_t secondaryText    = 0x987F99;
    std::uint32_t selector         = 0x9E7FBA;
    std::uint32_t track            = 0xD8C9DF;
    std::uint32_t trackEditing     = 0x806696;
    std::uint32_t accent           = 0x9E7FBA;
    std::uint32_t accentEditing    = 0xE9DAF8;
    std::uint32_t controlHighlight = 0xFFFFFF;
    std::uint32_t disabledSurface  = 0xEEEEEE;
    std::uint32_t disabledText     = 0xAAAAAA;
};

inline constexpr Theme DefaultTheme{};

struct ItemVisualState {
    Frame frame;
    const Theme& theme;
    bool focused  = false;
    bool editing  = false;
    float editMix = 0.0F;
};

enum class ItemActivation {
    Handled,
    BeginEdit,
};

class MenuItem {
public:
    explicit MenuItem(std::string_view label) : _label(label)
    {
    }
    virtual ~MenuItem() = default;

    MenuItem(const MenuItem&)            = delete;
    MenuItem& operator=(const MenuItem&) = delete;

    const std::string_view& label() const
    {
        return _label;
    }

    const Frame& frame() const
    {
        return _frame;
    }

    void setFrame(const Frame& frame)
    {
        _frame = frame;
    }

    virtual bool enabled() const
    {
        return true;
    }

    virtual ItemActivation activate()   = 0;
    virtual int preferredHeight() const = 0;
    virtual bool adjust(int direction)
    {
        (void)direction;
        return false;
    }

    virtual void beginEdit()
    {
    }
    virtual void commitEdit()
    {
    }
    virtual void cancelEdit()
    {
    }

    virtual bool update(std::uint32_t currentTime)                                   = 0;
    virtual void draw(lgfx::LGFX_Sprite& canvas, const ItemVisualState& state) const = 0;

private:
    std::string_view _label;
    Frame _frame;
};

}  // namespace settings::view
