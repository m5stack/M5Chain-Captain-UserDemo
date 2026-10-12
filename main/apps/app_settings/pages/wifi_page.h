/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "settings_page.h"

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

namespace settings {

class WifiPage final : public SettingsPage {
public:
    explicit WifiPage(bool embedded = false) : _embedded(embedded)
    {
    }
    ~WifiPage() override;
    void draw(lgfx::LGFX_Sprite& canvas) const;
    bool configurationComplete() const
    {
        return _configuration_complete;
    }

    void init() override;
    void handleInput(input::KeyEvent event) override;
    void update(std::uint32_t currentTime) override;
    SettingsPageResult result() const override;
    std::uint32_t transitionBackgroundColor() const override;

private:
    void drawSetupMessage(lgfx::LGFX_Sprite& canvas) const;
    void drawSuccessMessage(lgfx::LGFX_Sprite& canvas) const;
    void drawStartError(lgfx::LGFX_Sprite& canvas) const;

    SettingsPageResult _result   = SettingsPageResult::Active;
    bool _embedded               = false;
    bool _ap_started             = false;
    bool _configuration_complete = false;
};

}  // namespace settings
