/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "date_time_editor_page.h"

#include "../view/menu/roller_item.h"

#include <algorithm>
#include <array>
#include <apps/common/audio/audio.h>
#include <apps/common/toast/toast.h>
#include <cstdio>
#include <mooncake_log.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view Tag  = "Settings-DateTime";
constexpr int FirstYear         = 2000;
constexpr int LastYear          = 2099;
constexpr int ContentOffsetY    = 12;
constexpr std::size_t YearCount = static_cast<std::size_t>(LastYear - FirstYear + 1);

template <std::size_t Count>
std::array<std::string, Count> makeNumericLabels(int firstValue, int width)
{
    std::array<std::string, Count> labels;
    for (std::size_t index = 0; index < Count; ++index) {
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "%0*d", width, firstValue + static_cast<int>(index));
        labels[index] = buffer;
    }
    return labels;
}

const std::array<std::string, YearCount>& yearLabels()
{
    static const auto Labels = makeNumericLabels<YearCount>(FirstYear, 4);
    return Labels;
}

const std::array<std::string, 12>& monthLabels()
{
    static const auto Labels = makeNumericLabels<12>(1, 2);
    return Labels;
}

const std::array<std::string, 31>& dayLabels()
{
    static const auto Labels = makeNumericLabels<31>(1, 2);
    return Labels;
}

const std::array<std::string, 24>& hour24Labels()
{
    static const auto Labels = makeNumericLabels<24>(0, 2);
    return Labels;
}

const std::array<std::string, 24>& hour12Labels()
{
    static const auto Labels = [] {
        std::array<std::string, 24> labels;
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const int hour         = static_cast<int>(index);
            const int display_hour = hour % 12 == 0 ? 12 : hour % 12;
            char buffer[8]         = {};
            std::snprintf(buffer, sizeof(buffer), "%d %s", display_hour, hour < 12 ? "AM" : "PM");
            labels[index] = buffer;
        }
        return labels;
    }();
    return Labels;
}

const std::array<std::string, 60>& minuteLabels()
{
    static const auto Labels = makeNumericLabels<60>(0, 2);
    return Labels;
}

template <std::size_t Count>
std::vector<std::string_view> makeViews(const std::array<std::string, Count>& labels, std::size_t count = Count)
{
    count = std::min(count, labels.size());
    std::vector<std::string_view> views;
    views.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        views.emplace_back(labels[index]);
    }
    return views;
}

bool isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month)
{
    static constexpr std::array<int, 12> MonthDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > static_cast<int>(MonthDays.size())) {
        return 0;
    }
    return month == 2 && isLeapYear(year) ? 29 : MonthDays[static_cast<std::size_t>(month - 1)];
}

std::uint8_t weekdayFor(int year, int month, int day)
{
    static constexpr std::array<int, 12> MonthOffsets = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        --year;
    }
    return static_cast<std::uint8_t>(
        (year + year / 4 - year / 100 + year / 400 + MonthOffsets[static_cast<std::size_t>(month - 1)] + day) % 7);
}

}  // namespace

namespace settings {

void DateTimeEditorPage::init()
{
    _result                  = SettingsPageResult::Active;
    _stage                   = Stage::Year;
    _advance_stage_requested = false;

    DateTime current;
    if (!GetHAL().getDateTime(current)) {
        current = DateTime{};
        mclog::tagWarn(Tag, "RTC time unavailable; using default date and time");
    }
    _draft         = current;
    _draft.second  = 0;
    _preferred_day = _draft.day;
    buildStage();
}

void DateTimeEditorPage::handleInput(input::KeyEvent event)
{
    if (event == input::KeyEvent::Home) {
        audio::play_ui_sfx(audio::UiSfx::Back);
        previousStage();
        return;
    }
    _menu_view.handleInput(event);
}

void DateTimeEditorPage::update(std::uint32_t currentTime)
{
    if (_advance_stage_requested) {
        _advance_stage_requested = false;
        advanceStage();
    }
    if (_result == SettingsPageResult::Active) {
        _menu_view.update(currentTime);
    }
}

SettingsPageResult DateTimeEditorPage::result() const
{
    return _result;
}

void DateTimeEditorPage::buildStage()
{
    std::string_view section_title;
    std::string_view field_label;
    std::vector<std::string_view> options;
    int selected_index = 0;
    bool wraps         = true;

    switch (_stage) {
        case Stage::Year:
            section_title  = "Set Date";
            field_label    = "Year";
            options        = makeViews(yearLabels());
            selected_index = static_cast<int>(_draft.year) - FirstYear;
            wraps          = false;
            break;
        case Stage::Month:
            section_title  = "Set Date";
            field_label    = "Month";
            options        = makeViews(monthLabels());
            selected_index = static_cast<int>(_draft.month) - 1;
            break;
        case Stage::Day:
            section_title  = "Set Date";
            field_label    = "Day";
            options        = makeViews(dayLabels(), static_cast<std::size_t>(daysInMonth(_draft.year, _draft.month)));
            selected_index = static_cast<int>(_draft.day) - 1;
            break;
        case Stage::Hour:
            section_title = "Set Time";
            field_label   = "Hour";
            options = GetHAL().deviceSettings().use24HourClock ? makeViews(hour24Labels()) : makeViews(hour12Labels());
            selected_index = _draft.hour;
            break;
        case Stage::Minute:
            section_title  = "Set Time";
            field_label    = "Minute";
            options        = makeViews(minuteLabels());
            selected_index = _draft.minute;
            break;
    }

    std::vector<view::MenuSection> sections;
    auto& section = sections.emplace_back(section_title);
    section.addItem<view::RollerItem>(
        field_label, std::move(options), selected_index, wraps, [this](int index) { updateStageValue(index); },
        [this](int index) {
            updateStageValue(index);
            _advance_stage_requested = true;
            return true;
        });
    _menu_view.init(std::move(sections), false, true, ContentOffsetY);
}

void DateTimeEditorPage::updateStageValue(int index)
{
    switch (_stage) {
        case Stage::Year:
            _draft.year = static_cast<std::uint16_t>(FirstYear + index);
            _draft.day  = static_cast<std::uint8_t>(std::min(_preferred_day, daysInMonth(_draft.year, _draft.month)));
            break;
        case Stage::Month:
            _draft.month = static_cast<std::uint8_t>(index + 1);
            _draft.day   = static_cast<std::uint8_t>(std::min(_preferred_day, daysInMonth(_draft.year, _draft.month)));
            break;
        case Stage::Day:
            _draft.day     = static_cast<std::uint8_t>(index + 1);
            _preferred_day = _draft.day;
            break;
        case Stage::Hour:
            _draft.hour = static_cast<std::uint8_t>(index);
            break;
        case Stage::Minute:
            _draft.minute = static_cast<std::uint8_t>(index);
            break;
    }
}

void DateTimeEditorPage::advanceStage()
{
    if (_stage == Stage::Minute) {
        if (apply()) {
            _result = SettingsPageResult::Applied;
        } else {
            buildStage();
        }
        return;
    }

    _stage = static_cast<Stage>(static_cast<std::uint8_t>(_stage) + 1);
    buildStage();
}

void DateTimeEditorPage::previousStage()
{
    _advance_stage_requested = false;
    if (_stage == Stage::Year) {
        _result = SettingsPageResult::Cancelled;
        return;
    }

    _stage = static_cast<Stage>(static_cast<std::uint8_t>(_stage) - 1);
    buildStage();
}

bool DateTimeEditorPage::apply()
{
    _draft.second  = 0;
    _draft.weekday = weekdayFor(_draft.year, _draft.month, _draft.day);
    if (GetHAL().setDateTime(_draft)) {
        ::view::pop_a_toast("Date & time set", ::view::ToastType::Success);
        return true;
    }

    mclog::tagError(Tag, "failed to set date and time");
    return false;
}

}  // namespace settings
