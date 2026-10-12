/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "menu_page.h"

#include <apps/common/toast/toast.h>
#include <utility>

namespace settings {

MenuPage::MenuPage(std::vector<view::MenuSection> sections)
    : _sections(std::move(sections)), _background_only(_sections.empty())
{
}

MenuPage::~MenuPage()
{
    ::view::getToastManager().clear();
}

void MenuPage::init()
{
    _result             = SettingsPageResult::Active;
    auto& toast_manager = ::view::getToastManager();
    toast_manager.clear();
    _menu_view.init(std::move(_sections), false);
    _menu_view.setOverlayDrawCallback([](lgfx::LGFX_Sprite& canvas) { ::view::getToastManager().draw(canvas); });
}

void MenuPage::handleInput(input::KeyEvent event)
{
    if (_menu_view.handleInput(event)) {
        return;
    }

    if (event == input::KeyEvent::Home) {
        _result = SettingsPageResult::Applied;
    }
}

void MenuPage::update(std::uint32_t currentTime)
{
    auto& toast_manager      = ::view::getToastManager();
    const bool toast_changed = toast_manager.update(currentTime);
    _menu_view.update(currentTime);
    if (toast_changed || toast_manager.hasPending()) {
        _menu_view.redraw();
    }
}

SettingsPageResult MenuPage::result() const
{
    return _result;
}

bool MenuPage::redraw()
{
    _menu_view.redraw();
    return true;
}

TransitionSurface MenuPage::transitionSurface() const
{
    return _background_only ? TransitionSurface::Background : TransitionSurface::Snapshot;
}

std::uint32_t MenuPage::transitionBackgroundColor() const
{
    return view::DefaultTheme.background;
}

}  // namespace settings
