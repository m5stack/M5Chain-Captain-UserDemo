/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_chain_iot.h"
#include "view/menu/menu_view.h"

#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace {

constexpr std::uint32_t SnapshotInterval        = 100;
constexpr std::uint32_t InitialSnapshotInterval = 16;

}  // namespace

AppChainIot::AppChainIot()
{
    auto& app_info    = setAppInfo();
    app_info.name     = "Chain IoT";
    app_info.icon     = const_cast<gfx_image_dsc_t*>(&image_app_chain_iot);
    app_info.userData = &_theme_color;
}

AppChainIot::~AppChainIot() = default;

void AppChainIot::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppChainIot::onOpen()
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
    if (!GetHAL().beginChain()) {
        mclog::tagError(getAppInfo().name, "failed to start Chain buses");
        close();
        return;
    }

    if (!GetHAL().startWifiStation()) {
        mclog::tagWarn(getAppInfo().name, "Wi-Fi unavailable; Chain controls remain offline");
    }
    if (!GetHAL().beginChainEzData()) {
        mclog::tagWarn(getAppInfo().name, "failed to start EzData");
    }

    _key_manager = {};
    _view.reset();
    updateSnapshot();
    if (_view == nullptr) chain_iot::view::ChainMenuView::drawLoading();
}

void AppChainIot::onRunning()
{
    const auto event = _key_manager.update();
    if ((_view == nullptr || !_view->handleInput(event)) && event == input::KeyEvent::Home) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        close();
        return;
    }

    const auto now = GetHAL().millis();
    if (static_cast<std::int32_t>(now - _next_snapshot_at) >= 0) {
        updateSnapshot();
    }
    if (_view != nullptr) _view->update(GetHAL().millis());
}

void AppChainIot::updateSnapshot()
{
    const auto snapshot = GetHAL().getChainSnapshot();
    _next_snapshot_at =
        snapshot.capturedAt + (snapshot.initialScanComplete ? SnapshotInterval : InitialSnapshotInterval);
    if (!snapshot.initialScanComplete) return;

    GetHAL().publishChainEzDataSnapshot(snapshot);

    if (_view == nullptr) {
        _view = std::make_unique<chain_iot::view::ChainMenuView>();
        _view->init(snapshot);
    } else {
        _view->applySnapshot(snapshot);
    }
}

void AppChainIot::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().endChainEzData();
    GetHAL().stopWifiStation();
    GetHAL().endChain();
    GetHAL().setDisplayRenderer(DisplayRenderer::Gfx);
    _view.reset();
    if (_power_button_double_click_guard_active) {
        if (!GetHAL().setPowerButtonDoubleClickEnabled(_power_button_double_click_was_enabled)) {
            mclog::tagWarn(getAppInfo().name, "failed to restore power button double-click shutdown");
        }
        _power_button_double_click_guard_active = false;
    }
}
