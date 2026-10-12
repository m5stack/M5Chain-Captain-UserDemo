/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "pages/about_page.h"
#include "pages/controls_test_page.h"
#include "pages/menu_page.h"
#include "pages/rgb_test_page.h"
#include "pages/wifi_page.h"
#include "view/menu/menu_view.h"
#include "view/page_transition.h"

#include <apps/common/key_manager/key_manager.h>
#include <apps/common/screensaver/controller.h>
#include <cstdint>
#include <memory>
#include <mooncake.h>
#include <vector>

class AppSettings : public mooncake::AppAbility {
public:
    AppSettings();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class DevicePage {
        None,
        Display,
        Sound,
        DateTime,
    };

    enum class NetworkPage {
        None,
        Wifi,
    };

    void openRootMenu();
    settings::SettingsPage* activePage();
    void pushPage(std::unique_ptr<settings::SettingsPage> page);
    void beginOpenPage();
    void beginClosePage();
    void finishClosePage();
    void requestDevicePage(DevicePage page);
    void openDevicePage(DevicePage page);
    void requestNetworkPage(NetworkPage page);
    void openNetworkPage(NetworkPage page);
    void openDateTimeEditorPage();
    void openAboutPage();
    void openControlsTestPage();
    void openRgbTestPage();

    std::uint32_t _theme_color = settings::view::DefaultTheme.background;
    input::KeyManager _key_manager;
    screensaver::Controller _screensaver;
    std::unique_ptr<settings::view::MenuView> _menu_view;
    settings::view::PageTransition _page_transition;
    std::vector<std::unique_ptr<settings::SettingsPage>> _page_stack;
    DevicePage _open_device_page_requested       = DevicePage::None;
    NetworkPage _open_network_page_requested     = NetworkPage::None;
    bool _active_page_initialized                = false;
    bool _open_date_time_editor_requested        = false;
    bool _open_about_requested                   = false;
    std::uint32_t _version_clicked_at            = 0;
    std::uint8_t _version_clicks                 = 0;
    bool _open_controls_test_requested           = false;
    bool _open_rgb_test_requested                = false;
    bool _power_button_double_click_was_enabled  = true;
    bool _power_button_double_click_guard_active = false;
    bool _preview_screensaver_requested          = false;
    // Set once all buttons are released after a preview starts; the press that
    // opened the preview must not instantly wake it.
    bool _preview_released = false;
};
