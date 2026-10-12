/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../chain_node.h"
#include "../icon_cache.h"
#include <apps/app_settings/view/menu/menu_view.h>
#include <apps/app_settings/view/menu/action_item.h>
#include <apps/common/status_bar/status_bar.h>

namespace chain_iot::view {

class CaptainNode final : public ChainNode {
public:
    CaptainNode();
    std::uint32_t id() const override;
    NodeKind kind() const override;
    std::string_view title() const override;
    bool detailAvailable() const override;
    DetailLayout detailLayout() const override;
    bool apply(const ChainSnapshot& snapshot) override;
    void prepareIcon(lgfx::LGFX_Sprite& canvas) const override;
    void drawIcon(lgfx::LGFX_Sprite& canvas, int centerX, int centerY) const override;
    void drawInfo(lgfx::LGFX_Sprite& canvas, int panelY) const override;
    bool handleDetailInput(input::KeyEvent event) override;
    bool updateDetail(std::uint32_t currentTime) override;
    void drawDetail(lgfx::LGFX_Sprite& canvas, const Frame& frame) const override;

private:
    enum class DetailPage { Menu, WaitingForWifi, WifiRequired, Guide };
    void openConfiguration();
    void returnToMenu();

    settings::view::MenuView _menu;
    DetailPage _page = DetailPage::Menu;
    ::view::StatusBar _status_bar;
    settings::view::ActionItem* _configure_item = nullptr;
    bool _detail_dirty                          = true;
    std::size_t _connected                      = 0;
    EzDataStatus _ezdata;
    mutable OpaqueIconCache _icon_cache{52, 36};
};

}  // namespace chain_iot::view
