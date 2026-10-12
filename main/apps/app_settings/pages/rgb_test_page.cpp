/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rgb_test_page.h"

#include "../view/menu/rgb_slider_item.h"
#include "../view/menu/slider_item.h"

#include <algorithm>
#include <hal/hal.h>
#include <limits>
#include <mooncake_log.h>
#include <utility>
#include <vector>

namespace {

constexpr char Tag[]         = "RGB Test";
constexpr int ChannelMinimum = 0;
constexpr int ChannelMaximum = 100;
constexpr int ChannelStep    = 10;

std::uint8_t channelValue(int level)
{
    const int clamped = std::clamp(level, ChannelMinimum, ChannelMaximum);
    return static_cast<std::uint8_t>((clamped * 255 + ChannelMaximum / 2) / ChannelMaximum);
}

int channelLevel(std::uint8_t value)
{
    const int percentage = (static_cast<int>(value) * ChannelMaximum + 127) / 255;
    return std::clamp(((percentage + ChannelStep / 2) / ChannelStep) * ChannelStep, ChannelMinimum, ChannelMaximum);
}

RgbColor unpackColor(std::uint32_t color)
{
    return {.red   = static_cast<std::uint8_t>((color >> 16U) & 0xFFU),
            .green = static_cast<std::uint8_t>((color >> 8U) & 0xFFU),
            .blue  = static_cast<std::uint8_t>(color & 0xFFU)};
}

int nearestPreset(RgbColor color)
{
    int nearest  = 0;
    int distance = std::numeric_limits<int>::max();
    for (int index = 0; index < settings::view::RgbSliderItem::PresetCount; ++index) {
        const auto preset = unpackColor(settings::view::RgbSliderItem::presetColor(index));
        const int red     = static_cast<int>(color.red) - preset.red;
        const int green   = static_cast<int>(color.green) - preset.green;
        const int blue    = static_cast<int>(color.blue) - preset.blue;
        const int next    = red * red + green * green + blue * blue;
        if (next < distance) {
            distance = next;
            nearest  = index;
        }
    }
    return nearest;
}

}  // namespace

namespace settings {

RgbTestPage::~RgbTestPage()
{
    if (!GetHAL().setRgbColor(0, 0, 0)) {
        mclog::tagWarn(Tag, "failed to turn RGB light off");
    }
}

void RgbTestPage::init()
{
    _result = SettingsPageResult::Active;
    _red = _green = _blue = ChannelMinimum;
    _rgb_slider           = nullptr;
    _red_slider = _green_slider = _blue_slider = nullptr;
    if (!applyColor()) {
        mclog::tagWarn(Tag, "failed to initialize RGB light");
    }

    std::vector<view::MenuSection> sections;
    auto& rgb   = sections.emplace_back("RGB Test");
    _rgb_slider = &rgb.addItem<view::RgbSliderItem>(
        0,
        [this](int index) {
            if (!setPreset(index)) {
                mclog::tagError(Tag, "failed to update RGB preset");
            }
        },
        [this](int index) {
            if (!setPreset(index)) {
                mclog::tagError(Tag, "failed to restore RGB preset");
            }
        });
    _red_slider = &rgb.addItem<view::SliderItem>(
        "Red", _red, ChannelMinimum, ChannelMaximum, ChannelStep,
        [this](int value) {
            if (!setChannel(_red, value, _red_slider)) {
                mclog::tagError(Tag, "failed to update red channel");
            }
        },
        view::SliderItem::CommittedCallback{},
        [this](int value) {
            if (!setChannel(_red, value, _red_slider)) {
                mclog::tagError(Tag, "failed to restore red channel");
            }
        });
    _green_slider = &rgb.addItem<view::SliderItem>(
        "Green", _green, ChannelMinimum, ChannelMaximum, ChannelStep,
        [this](int value) {
            if (!setChannel(_green, value, _green_slider)) {
                mclog::tagError(Tag, "failed to update green channel");
            }
        },
        view::SliderItem::CommittedCallback{},
        [this](int value) {
            if (!setChannel(_green, value, _green_slider)) {
                mclog::tagError(Tag, "failed to restore green channel");
            }
        });
    _blue_slider = &rgb.addItem<view::SliderItem>(
        "Blue", _blue, ChannelMinimum, ChannelMaximum, ChannelStep,
        [this](int value) {
            if (!setChannel(_blue, value, _blue_slider)) {
                mclog::tagError(Tag, "failed to update blue channel");
            }
        },
        view::SliderItem::CommittedCallback{},
        [this](int value) {
            if (!setChannel(_blue, value, _blue_slider)) {
                mclog::tagError(Tag, "failed to restore blue channel");
            }
        });
    _menu_view.init(std::move(sections), false);
}

void RgbTestPage::handleInput(input::KeyEvent event)
{
    if (_menu_view.handleInput(event)) {
        return;
    }
    if (event == input::KeyEvent::Home) {
        _result = SettingsPageResult::Applied;
    }
}

void RgbTestPage::update(std::uint32_t currentTime)
{
    _menu_view.update(currentTime);
}

SettingsPageResult RgbTestPage::result() const
{
    return _result;
}

bool RgbTestPage::redraw()
{
    _menu_view.redraw();
    return true;
}

bool RgbTestPage::setChannel(int& channel, int value, view::SliderItem* slider)
{
    const int previous = channel;
    channel            = std::clamp(value, ChannelMinimum, ChannelMaximum);
    if (_syncing_channel_items) {
        return true;
    }
    if (applyColor()) {
        if (_rgb_slider != nullptr) {
            _rgb_slider->setIndex(
                nearestPreset(RgbColor{channelValue(_red), channelValue(_green), channelValue(_blue)}), false);
        }
        return true;
    }

    channel = previous;
    if (slider != nullptr) {
        _syncing_channel_items = true;
        slider->setValue(previous, false);
        _syncing_channel_items = false;
    }
    mclog::tagError(Tag, "failed to update RGB light");
    return false;
}

bool RgbTestPage::setPreset(int index)
{
    const int next_index     = std::clamp(index, 0, view::RgbSliderItem::PresetCount - 1);
    const int previous_red   = _red;
    const int previous_green = _green;
    const int previous_blue  = _blue;
    const auto color         = unpackColor(view::RgbSliderItem::presetColor(next_index));
    _red                     = channelLevel(color.red);
    _green                   = channelLevel(color.green);
    _blue                    = channelLevel(color.blue);

    if (!applyColor()) {
        _red   = previous_red;
        _green = previous_green;
        _blue  = previous_blue;
        return false;
    }

    syncChannelItems();
    return true;
}

void RgbTestPage::syncChannelItems()
{
    _syncing_channel_items = true;
    if (_red_slider != nullptr) {
        _red_slider->setValue(_red, false);
    }
    if (_green_slider != nullptr) {
        _green_slider->setValue(_green, false);
    }
    if (_blue_slider != nullptr) {
        _blue_slider->setValue(_blue, false);
    }
    _syncing_channel_items = false;
}

bool RgbTestPage::applyColor() const
{
    return GetHAL().setRgbColor(channelValue(_red), channelValue(_green), channelValue(_blue));
}

}  // namespace settings
