/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_settings.h"

#include "pages/date_time_editor_page.h"
#include "view/menu/action_item.h"
#include "view/menu/roller_item.h"
#include "view/menu/segmented_item.h"
#include "view/menu/slider_item.h"
#include "view/menu/switch_item.h"

#include <algorithm>
#include <array>
#include <apps/common/audio/audio.h>
#include <apps/common/common.h>
#include <apps/common/screensaver/screensaver.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct TimeZoneOption {
    std::string_view label;
    std::string_view value;
};

constexpr std::array<TimeZoneOption, 27> TimeZoneOptions = {{
    {"UTC-12", "UTC+12"}, {"UTC-11", "UTC+11"}, {"UTC-10", "UTC+10"}, {"UTC-09", "UTC+9"},  {"UTC-08", "UTC+8"},
    {"UTC-07", "UTC+7"},  {"UTC-06", "UTC+6"},  {"UTC-05", "UTC+5"},  {"UTC-04", "UTC+4"},  {"UTC-03", "UTC+3"},
    {"UTC-02", "UTC+2"},  {"UTC-01", "UTC+1"},  {"UTC", "UTC0"},      {"UTC+01", "UTC-1"},  {"UTC+02", "UTC-2"},
    {"UTC+03", "UTC-3"},  {"UTC+04", "UTC-4"},  {"UTC+05", "UTC-5"},  {"UTC+06", "UTC-6"},  {"UTC+07", "UTC-7"},
    {"UTC+08", "UTC-8"},  {"UTC+09", "UTC-9"},  {"UTC+10", "UTC-10"}, {"UTC+11", "UTC-11"}, {"UTC+12", "UTC-12"},
    {"UTC+13", "UTC-13"}, {"UTC+14", "UTC-14"},
}};

std::vector<std::string_view> timeZoneLabels()
{
    std::vector<std::string_view> labels;
    labels.reserve(TimeZoneOptions.size());
    for (const auto& option : TimeZoneOptions) {
        labels.push_back(option.label);
    }
    return labels;
}

int timeZoneIndex(std::string_view value)
{
    for (std::size_t index = 0; index < TimeZoneOptions.size(); ++index) {
        if (TimeZoneOptions[index].value == value) {
            return static_cast<int>(index);
        }
    }
    return 12;
}

const TimeZoneOption& timeZoneOption(int index)
{
    return TimeZoneOptions[static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(TimeZoneOptions.size()) - 1))];
}

}  // namespace

AppSettings::AppSettings()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "Settings";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_settings);
    app_info.userData = &_theme_color;
}

void AppSettings::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppSettings::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _power_button_double_click_was_enabled  = GetHAL().isPowerButtonDoubleClickEnabled();
    _power_button_double_click_guard_active = GetHAL().setPowerButtonDoubleClickEnabled(false);
    if (!_power_button_double_click_guard_active) {
        mclog::tagWarn(getAppInfo().name, "failed to disable power button double-click shutdown");
    }

    if (!GetHAL().setDisplayRenderer(DisplayRenderer::Gfx)) {
        mclog::tagError(getAppInfo().name, "failed to activate GFX renderer");
        close();
        return;
    }

    openRootMenu();
}

void AppSettings::onRunning()
{
    const input::KeyEvent event      = _key_manager.update();
    const std::uint32_t current_time = GetHAL().millis();

    if (_screensaver.active()) {
        // Ignore the held button that opened the preview; wait for a full
        // release before any press counts as a wake.
        if (!_preview_released) {
            const bool any_held = GetHAL().btnPwr.isPressed() || GetHAL().btnA.isPressed() ||
                                  GetHAL().btnB.isPressed() || GetHAL().btnC.isPressed();
            if (event == input::KeyEvent::None && !any_held) {
                _preview_released = true;
            }
        }
        const bool activity =
            _preview_released && (event != input::KeyEvent::None || GetHAL().btnPwr.isPressed() ||
                                  GetHAL().btnA.isPressed() || GetHAL().btnB.isPressed() || GetHAL().btnC.isPressed());
        if (_screensaver.update(current_time, activity)) {
            if (!_screensaver.active()) {
                // The wake press is consumed; restore the page behind the preview.
                if (auto* page = activePage(); page != nullptr) {
                    page->redraw();
                } else if (_menu_view != nullptr) {
                    _menu_view->redraw();
                }
            }
            return;
        }
    }

    if (_page_transition.active()) {
        const auto completed_direction = _page_transition.update(current_time);
        if (completed_direction == settings::view::PageTransition::Direction::Push) {
            if (!_active_page_initialized) {
                activePage()->init();
                _active_page_initialized = true;
            }
        } else if (completed_direction == settings::view::PageTransition::Direction::Pop) {
            finishClosePage();
        }
        return;
    }

    if (auto* page = activePage(); page != nullptr) {
        page->handleInput(event);
        page->update(current_time);

        if (_preview_screensaver_requested) {
            _preview_screensaver_requested = false;
            _preview_released              = false;
            _screensaver.preview(current_time);
            return;
        }
        if (_open_date_time_editor_requested) {
            openDateTimeEditorPage();
            return;
        }
        if (page->result() != settings::SettingsPageResult::Active) {
            beginClosePage();
        }
        return;
    }

    if (_menu_view != nullptr) {
        if (event == input::KeyEvent::Previous || event == input::KeyEvent::Next ||
            event == input::KeyEvent::PreviousRepeat || event == input::KeyEvent::NextRepeat ||
            event == input::KeyEvent::Home) {
            _version_clicks = 0;
        }
        if (!_menu_view->handleInput(event) && event == input::KeyEvent::Home) {
            close();
            return;
        }
        _menu_view->update(current_time);
    } else if (event == input::KeyEvent::Home) {
        close();
        return;
    }

    if (_open_device_page_requested != DevicePage::None) {
        openDevicePage(_open_device_page_requested);
    } else if (_open_network_page_requested != NetworkPage::None) {
        openNetworkPage(_open_network_page_requested);
    } else if (_open_about_requested) {
        openAboutPage();
    }
    if (_open_controls_test_requested) {
        openControlsTestPage();
    }
    if (_open_rgb_test_requested) {
        openRgbTestPage();
    }
}

void AppSettings::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _page_stack.clear();
    _menu_view.reset();
    _page_transition.reset();
    _screensaver.reset(GetHAL().millis());
    _preview_screensaver_requested = false;
    GetHAL().setGfxPresentSuppressed(false);
    _open_device_page_requested      = DevicePage::None;
    _open_network_page_requested     = NetworkPage::None;
    _active_page_initialized         = false;
    _open_date_time_editor_requested = false;
    _open_about_requested            = false;
    _open_controls_test_requested    = false;
    _open_rgb_test_requested         = false;
    if (_power_button_double_click_guard_active) {
        if (!GetHAL().setPowerButtonDoubleClickEnabled(_power_button_double_click_was_enabled)) {
            mclog::tagWarn(getAppInfo().name, "failed to restore power button double-click shutdown");
        }
        _power_button_double_click_guard_active = false;
    }
}

void AppSettings::openRootMenu()
{
    _version_clicks = 0;
    using settings::view::ActionItem;
    using settings::view::MenuSection;

    std::vector<MenuSection> sections;
    auto& device = sections.emplace_back("Device");
    device.addItem<ActionItem>("Display", [this]() { requestDevicePage(DevicePage::Display); });
    device.addItem<ActionItem>("Sound", [this]() { requestDevicePage(DevicePage::Sound); });
    device.addItem<ActionItem>("Date & Time", [this]() { requestDevicePage(DevicePage::DateTime); });

    auto& wifi = sections.emplace_back("Wi-Fi");
    wifi.addItem<ActionItem>("Configure Wi-Fi", [this]() { requestNetworkPage(NetworkPage::Wifi); });

    auto& hardware = sections.emplace_back("Hardware Test");
    hardware.addItem<ActionItem>("RGB Test", [this]() { _open_rgb_test_requested = true; });

    auto& system = sections.emplace_back("System");
    system.addItem<ActionItem>(common::FirmwareVersionLabel, [this]() {
        const auto now = GetHAL().millis();
        if (now - _version_clicked_at > 900) _version_clicks = 0;
        _version_clicked_at = now;
        if (++_version_clicks == 5) {
            _version_clicks       = 0;
            _open_about_requested = true;
        }
    });

    // auto& development = sections.emplace_back("Development");
    // development.addItem<ActionItem>("Controls Lab", [this]() { _open_controls_test_requested = true; });

    _menu_view = std::make_unique<settings::view::MenuView>();
    _menu_view->init(std::move(sections));
}

settings::SettingsPage* AppSettings::activePage()
{
    return _page_stack.empty() ? nullptr : _page_stack.back().get();
}

void AppSettings::pushPage(std::unique_ptr<settings::SettingsPage> page)
{
    if (page == nullptr) {
        return;
    }

    _page_stack.push_back(std::move(page));
    beginOpenPage();
}

void AppSettings::beginOpenPage()
{
    auto* page = activePage();
    if (page == nullptr) {
        return;
    }

    _active_page_initialized = false;
    if (!_page_transition.captureRoot()) {
        mclog::tagWarn(getAppInfo().name, "page transition buffers unavailable, switching directly");
        page->init();
        _active_page_initialized = true;
        return;
    }

    if (page->transitionSurface() == settings::TransitionSurface::Snapshot) {
        GetHAL().setGfxPresentSuppressed(true);
        page->init();
        _active_page_initialized = true;
        GetHAL().setGfxPresentSuppressed(false);
        if (!_page_transition.capturePage()) {
            mclog::tagWarn(getAppInfo().name, "failed to capture page transition target");
            GetHAL().presentGfx();
            return;
        }
    }
    _page_transition.beginPush(page->transitionSurface(), page->transitionBackgroundColor());
}

void AppSettings::beginClosePage()
{
    auto* closing_page = activePage();
    if (!_active_page_initialized || closing_page == nullptr) {
        finishClosePage();
        return;
    }

    if (!_page_transition.capturePage()) {
        finishClosePage();
        return;
    }

    GetHAL().setGfxPresentSuppressed(true);
    bool parent_rendered = false;
    if (_page_stack.size() > 1) {
        parent_rendered = _page_stack[_page_stack.size() - 2]->redraw();
    } else if (_menu_view != nullptr) {
        _menu_view->redraw();
        parent_rendered = true;
    }
    const bool parent_captured = parent_rendered && _page_transition.captureRoot();
    GetHAL().setGfxPresentSuppressed(false);

    if (!parent_captured) {
        mclog::tagWarn(getAppInfo().name, "failed to capture page transition parent");
        finishClosePage();
        return;
    }
    _page_transition.beginPop(closing_page->transitionBackgroundColor());
}

void AppSettings::finishClosePage()
{
    if (!_page_stack.empty()) {
        _page_stack.pop_back();
    }
    _active_page_initialized = !_page_stack.empty();

    if (auto* page = activePage(); page != nullptr) {
        if (!page->redraw()) {
            mclog::tagWarn(getAppInfo().name, "parent page does not support redraw");
        }
    } else if (_menu_view != nullptr) {
        _menu_view->redraw();
    }
}

void AppSettings::requestDevicePage(DevicePage page)
{
    if (_menu_view != nullptr) {
        _open_device_page_requested = page;
    }
}

void AppSettings::requestNetworkPage(NetworkPage page)
{
    if (_menu_view != nullptr) {
        _open_network_page_requested = page;
    }
}

void AppSettings::openNetworkPage(NetworkPage page)
{
    _open_network_page_requested = NetworkPage::None;
    if (page == NetworkPage::None) {
        return;
    }

    pushPage(std::make_unique<settings::WifiPage>());
}

void AppSettings::openDevicePage(DevicePage page)
{
    using settings::view::ActionItem;
    using settings::view::MenuSection;
    using settings::view::RollerItem;
    using settings::view::SegmentedItem;
    using settings::view::SliderItem;
    using settings::view::SwitchItem;

    _open_device_page_requested           = DevicePage::None;
    const DeviceSettings current_settings = GetHAL().deviceSettings();
    std::vector<MenuSection> sections;

    switch (page) {
        case DevicePage::Display: {
            auto& display = sections.emplace_back("Display");
            display.addItem<SliderItem>(
                "Brightness", current_settings.backlightBrightness, 10, 100, 5,
                [this](int brightness) {
                    if (!GetHAL().setBackLightBrightness(brightness)) {
                        mclog::tagError(getAppInfo().name, "failed to preview brightness: {}", brightness);
                    }
                },
                [this](int brightness) {
                    const int saved_brightness        = GetHAL().deviceSettings().backlightBrightness;
                    DeviceSettings next_settings      = GetHAL().deviceSettings();
                    next_settings.backlightBrightness = brightness;
                    if (!GetHAL().saveDeviceSettings(next_settings)) {
                        mclog::tagError(getAppInfo().name, "failed to save brightness");
                        GetHAL().setBackLightBrightness(saved_brightness);
                        return false;
                    }
                    return true;
                },
                [this](int brightness) {
                    if (!GetHAL().setBackLightBrightness(brightness)) {
                        mclog::tagError(getAppInfo().name, "failed to restore brightness: {}", brightness);
                    }
                });
            display.addItem<SegmentedItem>(
                "Rotation", std::vector<std::string_view>{"0", "90", "180", "270"}, current_settings.displayRotation,
                false, SegmentedItem::ChangedCallback{}, [this](int rotation) -> bool {
                    const int saved_rotation = GetHAL().deviceSettings().displayRotation;
                    if (!GetHAL().setDisplayRotation(rotation)) {
                        mclog::tagError(getAppInfo().name, "failed to apply display rotation: {}", rotation);
                        return false;
                    }

                    DeviceSettings next_settings  = GetHAL().deviceSettings();
                    next_settings.displayRotation = rotation;
                    if (!GetHAL().saveDeviceSettings(next_settings)) {
                        mclog::tagError(getAppInfo().name, "failed to save display rotation");
                        GetHAL().setDisplayRotation(saved_rotation);
                        return false;
                    }
                    return true;
                });
            std::vector<std::string_view> saver_labels;
            int selected_saver = 0;
            const auto& savers = screensaver::registry();
            for (std::size_t index = 0; index < savers.size(); ++index) {
                saver_labels.push_back(savers[index].label);
                if (savers[index].id == current_settings.screensaver) {
                    selected_saver = static_cast<int>(index);
                }
            }
            display.addItem<RollerItem>("Screensaver", std::move(saver_labels), selected_saver, false,
                                        RollerItem::ChangedCallback{}, [this](int index) {
                                            DeviceSettings next_settings = GetHAL().deviceSettings();
                                            next_settings.screensaver =
                                                screensaver::registry()[static_cast<std::size_t>(index)].id;
                                            if (!GetHAL().saveDeviceSettings(next_settings)) {
                                                mclog::tagError(getAppInfo().name, "failed to save screensaver");
                                                return false;
                                            }
                                            return true;
                                        });
            display.addItem<ActionItem>("Preview Screensaver", [this]() { _preview_screensaver_requested = true; });
            break;
        }
        case DevicePage::Sound: {
            auto& sound = sections.emplace_back("Sound");
            sound.addItem<SliderItem>(
                "Volume", current_settings.speakerVolume, 0, 100, 5,
                [this](int volume) {
                    if (!GetHAL().setSpeakerVolume(volume)) {
                        mclog::tagError(getAppInfo().name, "failed to preview volume: {}", volume);
                    }
                },
                [this](int volume) {
                    const int saved_volume       = GetHAL().deviceSettings().speakerVolume;
                    DeviceSettings next_settings = GetHAL().deviceSettings();
                    next_settings.speakerVolume  = volume;
                    if (!GetHAL().saveDeviceSettings(next_settings)) {
                        mclog::tagError(getAppInfo().name, "failed to save volume");
                        GetHAL().setSpeakerVolume(saved_volume);
                        return false;
                    }
                    return true;
                },
                [this](int volume) {
                    if (!GetHAL().setSpeakerVolume(volume)) {
                        mclog::tagError(getAppInfo().name, "failed to restore volume: {}", volume);
                    }
                });
            sound.addItem<SwitchItem>("Key Sounds", current_settings.buttonSfxEnabled, [this](bool enabled) {
                DeviceSettings next_settings   = GetHAL().deviceSettings();
                next_settings.buttonSfxEnabled = enabled;
                if (!GetHAL().saveDeviceSettings(next_settings)) {
                    mclog::tagError(getAppInfo().name, "failed to save button sounds");
                    return false;
                }
                if (enabled) {
                    audio::play_ui_sfx(audio::UiSfx::Confirm);
                }
                return true;
            });
            break;
        }
        case DevicePage::DateTime: {
            auto& date_time = sections.emplace_back("Date & Time");
            date_time.addItem<ActionItem>("Set Date & Time", [this]() { _open_date_time_editor_requested = true; });
            date_time.addItem<RollerItem>("Time Zone", timeZoneLabels(), timeZoneIndex(current_settings.timezone), true,
                                          RollerItem::ChangedCallback{}, [this](int index) -> bool {
                                              const auto& option             = timeZoneOption(index);
                                              const std::string old_timezone = GetHAL().deviceSettings().timezone;
                                              if (!GetHAL().setTimezone(option.value)) {
                                                  mclog::tagError(getAppInfo().name, "failed to apply timezone: {}",
                                                                  option.value);
                                                  return false;
                                              }

                                              DeviceSettings next_settings = GetHAL().deviceSettings();
                                              next_settings.timezone       = option.value;
                                              if (!GetHAL().saveDeviceSettings(next_settings)) {
                                                  mclog::tagError(getAppInfo().name, "failed to save timezone");
                                                  GetHAL().setTimezone(old_timezone);
                                                  return false;
                                              }
                                              return true;
                                          });
            date_time.addItem<SwitchItem>("Use 24-Hour Clock", current_settings.use24HourClock, [this](bool enabled) {
                DeviceSettings next_settings = GetHAL().deviceSettings();
                next_settings.use24HourClock = enabled;
                if (!GetHAL().saveDeviceSettings(next_settings)) {
                    mclog::tagError(getAppInfo().name, "failed to save clock format");
                    return false;
                }
                return true;
            });
            date_time.addItem<SwitchItem>(
                "Automatic Wi-Fi Time Sync", current_settings.autoTimeSync, [this](bool enabled) {
                    DeviceSettings next_settings = GetHAL().deviceSettings();
                    next_settings.autoTimeSync   = enabled;
                    if (!GetHAL().saveDeviceSettings(next_settings)) {
                        mclog::tagError(getAppInfo().name, "failed to save automatic time synchronization");
                        return false;
                    }
                    return true;
                });
            break;
        }
        case DevicePage::None:
            return;
    }

    pushPage(std::make_unique<settings::MenuPage>(std::move(sections)));
}

void AppSettings::openDateTimeEditorPage()
{
    _open_date_time_editor_requested = false;
    pushPage(std::make_unique<settings::DateTimeEditorPage>());
}

void AppSettings::openAboutPage()
{
    _open_about_requested = false;
    pushPage(std::make_unique<settings::AboutPage>());
}

void AppSettings::openControlsTestPage()
{
    _open_controls_test_requested = false;
    pushPage(std::make_unique<settings::ControlsTestPage>());
}

void AppSettings::openRgbTestPage()
{
    _open_rgb_test_requested = false;
    pushPage(std::make_unique<settings::RgbTestPage>());
}
