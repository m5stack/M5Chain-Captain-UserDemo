/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "controls_test_page.h"

#include "../view/menu/action_item.h"
#include "../view/menu/roller_item.h"
#include "../view/menu/segmented_item.h"
#include "../view/menu/slider_item.h"
#include "../view/menu/switch_item.h"

#include <apps/common/toast/toast.h>
#include <utility>
#include <vector>

namespace settings {

ControlsTestPage::~ControlsTestPage()
{
    ::view::getToastManager().clear();
}

void ControlsTestPage::init()
{
    _result             = SettingsPageResult::Active;
    auto& toast_manager = ::view::getToastManager();
    toast_manager.clear();

    std::vector<view::MenuSection> sections;
    auto& controls = sections.emplace_back("CONTROLS LAB");
    controls.addItem<view::SwitchItem>("Focus Mode", _switch_value, [this](bool value) {
        _switch_value = value;
        return true;
    });
    controls.addItem<view::SwitchItem>("Automatic Software Updates", _long_switch_value, [this](bool value) {
        _long_switch_value = value;
        return true;
    });
    controls.addItem<view::SliderItem>("Intensity", _slider_value, 0, 100, 5,
                                       [this](int value) { _slider_value = value; });
    controls.addItem<view::RollerItem>("Appearance", std::vector<std::string_view>{"System", "Light", "Dark"},
                                       _roller_value, true, [this](int value) { _roller_value = value; });
    controls.addItem<view::SegmentedItem>("Mode", std::vector<std::string_view>{"Auto", "Eco", "Boost"},
                                          _segmented_value, false, [this](int value) { _segmented_value = value; });

    auto& toast = sections.emplace_back("TOAST LAB");
    toast.addItem<view::ActionItem>("Short Toast",
                                    []() { ::view::pop_a_toast("Settings saved", ::view::ToastType::Success); });
    toast.addItem<view::ActionItem>(
        "Wrapped Toast", []() { ::view::pop_a_toast("This message wraps across two lines", ::view::ToastType::Info); });
    toast.addItem<view::ActionItem>("Ellipsis Toast", []() {
        ::view::pop_a_toast(
            "This notification is intentionally much longer than the available toast width and should end with an "
            "ellipsis",
            ::view::ToastType::Warning);
    });
    toast.addItem<view::ActionItem>("Stack Toasts", []() {
        ::view::pop_a_toast("First toast", ::view::ToastType::Info, 2400);
        ::view::pop_a_toast("Second toast", ::view::ToastType::Orange, 2400);
        ::view::pop_a_toast("Third toast", ::view::ToastType::Success, 2400);
    });

    _menu_view.init(std::move(sections), false);
    _menu_view.setOverlayDrawCallback([](lgfx::LGFX_Sprite& canvas) { ::view::getToastManager().draw(canvas); });
}

void ControlsTestPage::handleInput(input::KeyEvent event)
{
    if (_menu_view.handleInput(event)) {
        return;
    }

    if (event == input::KeyEvent::Home) {
        ::view::getToastManager().clear();
        _menu_view.redraw();
        _result = SettingsPageResult::Applied;
    }
}

void ControlsTestPage::update(std::uint32_t currentTime)
{
    auto& toast_manager      = ::view::getToastManager();
    const bool toast_changed = toast_manager.update(currentTime);
    _menu_view.update(currentTime);
    if (toast_changed || toast_manager.hasPending()) {
        _menu_view.redraw();
    }
}

SettingsPageResult ControlsTestPage::result() const
{
    return _result;
}

}  // namespace settings
