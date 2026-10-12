/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <apps/apps.h>
#include <cstdint>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <memory>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

using namespace mooncake;

void setup()
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    GetHAL().init();

    uitk::ui_hal::on_delay([](std::uint32_t ms) { GetHAL().delay(ms); });
    uitk::ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppChainIot>());
    GetMooncake().installApp(std::make_unique<AppPomodoroTimer>());
    GetMooncake().installApp(std::make_unique<AppImuBubbleLevel>());
    GetMooncake().installApp(std::make_unique<AppIrCopy>());
    GetMooncake().installApp(std::make_unique<AppRecorder>());
    GetMooncake().installApp(std::make_unique<AppSettings>());
}

void loop()
{
    GetHAL().feedTheDog();
    GetMooncake().update();
}
