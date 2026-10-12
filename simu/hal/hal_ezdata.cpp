/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_internal.h"

#include <mooncake_log.h>

#include <algorithm>
#include <optional>
#include <type_traits>

namespace {

constexpr auto Tag                          = "HAL-EzData";
constexpr std::uint32_t ConnectionStepDelay = 250;
constexpr std::uint32_t UploadInterval      = 1000;

struct Session {
    EzDataStatus status;
    std::uint32_t stateChangedAt = 0;
    std::uint32_t nextUploadAt   = 0;
    std::optional<ChainSnapshot> latest;
    std::optional<ChainSnapshot> uploaded;
};

Session session;

bool sameColor(RgbColor a, RgbColor b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

bool sameDevice(const ChainDeviceSnapshot& a, const ChainDeviceSnapshot& b)
{
    if (a.id != b.id || a.kind != b.kind) return false;
    return std::visit(
        [&b](const auto& value) {
            using Data           = std::decay_t<decltype(value)>;
            const auto* previous = std::get_if<Data>(&b.data);
            if (previous == nullptr || !sameColor(value.rgb, previous->rgb)) return false;
            if constexpr (std::is_same_v<Data, ChainKeyData>) {
                return value.pressed == previous->pressed;
            } else if constexpr (std::is_same_v<Data, ChainAngleData>) {
                return value.angleDegrees == previous->angleDegrees;
            } else if constexpr (std::is_same_v<Data, ChainJoystickData>) {
                return value.x == previous->x && value.y == previous->y && value.pressed == previous->pressed;
            } else if constexpr (std::is_same_v<Data, ChainBuzzerData>) {
                return value.frequency == previous->frequency && value.mode == previous->mode &&
                       value.duty == previous->duty && value.enabled == previous->enabled;
            } else if constexpr (std::is_same_v<Data, ChainEncoderData>) {
                return value.value == previous->value && value.delta == previous->delta &&
                       value.pressed == previous->pressed;
            } else if constexpr (std::is_same_v<Data, ChainMonoData> || std::is_same_v<Data, ChainRgbData>) {
                return value.mode == previous->mode && value.brightness == previous->brightness;
            } else if constexpr (std::is_same_v<Data, ChainPirData>) {
                return value.detected == previous->detected;
            } else if constexpr (std::is_same_v<Data, ChainTofData>) {
                return value.distanceMm == previous->distanceMm;
            } else {
                return true;
            }
        },
        a.data);
}

bool sameSnapshot(const ChainSnapshot& a, const ChainSnapshot& b)
{
    const auto same_side = [](const auto& left, const auto& right) {
        return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), sameDevice);
    };
    return same_side(a.left, b.left) && same_side(a.right, b.right);
}

}  // namespace

namespace hal::internal {

bool ezdataStart()
{
    if (session.status.state != EzDataState::Off) return true;
    session.status.state   = EzDataState::WaitingForWifi;
    session.stateChangedAt = GetHAL().millis();
    mclog::tagInfo(Tag, "simulator session started; waiting for Wi-Fi");
    return true;
}

void ezdataStop()
{
    if (session.status.state == EzDataState::Off) return;
    session = {};
    mclog::tagInfo(Tag, "simulator session stopped");
}

void ezdataUpdate(std::uint32_t now)
{
    if (session.status.state == EzDataState::Off) return;
    if (!GetHAL().isWifiConnected()) {
        const auto state =
            GetHAL().getWifiState() == WifiState::Failed ? EzDataState::Failed : EzDataState::WaitingForWifi;
        if (session.status.state != state) {
            session.status.state = state;
            session.status.pairingCode.clear();
            session.status.lastError = state == EzDataState::Failed ? "Wi-Fi connection failed" : "";
            session.stateChangedAt   = now;
            session.uploaded.reset();
            mclog::tagInfo(Tag, "simulator {}", state == EzDataState::Failed ? "Wi-Fi failed" : "waiting for Wi-Fi");
        }
        return;
    }

    switch (session.status.state) {
        case EzDataState::WaitingForWifi:
        case EzDataState::Failed:
            session.status.state = EzDataState::Registering;
            session.status.lastError.clear();
            session.stateChangedAt = now;
            mclog::tagInfo(Tag, "simulator registration started");
            return;
        case EzDataState::Registering:
            if (now - session.stateChangedAt < ConnectionStepDelay) return;
            session.status.state   = EzDataState::Connecting;
            session.stateChangedAt = now;
            mclog::tagInfo(Tag, "simulator MQTT connection started");
            return;
        case EzDataState::Connecting:
            if (now - session.stateChangedAt < ConnectionStepDelay) return;
            session.status.state       = EzDataState::Connected;
            session.status.pairingCode = "SIM123";
            session.stateChangedAt     = now;
            session.nextUploadAt       = now;
            mclog::tagInfo(Tag, "simulator connected; local-only pairing code: {}", session.status.pairingCode);
            break;
        case EzDataState::Connected:
            break;
        case EzDataState::Off:
            return;
    }

    if (!GetHAL().deviceSettings().ezDataUploadEnabled) {
        session.uploaded.reset();
        return;
    }

    if (!session.latest || static_cast<std::int32_t>(now - session.nextUploadAt) < 0) return;
    if (session.uploaded && sameSnapshot(*session.latest, *session.uploaded)) return;

    mclog::tagInfo(Tag, "simulator {} upload: {} left, {} right", session.uploaded ? "changed" : "full",
                   session.latest->left.size(), session.latest->right.size());
    session.uploaded     = session.latest;
    session.nextUploadAt = now + UploadInterval;
}

void ezdataPublishChainSnapshot(const ChainSnapshot& snapshot)
{
    if (session.status.state == EzDataState::Off || !snapshot.initialScanComplete) return;
    session.latest = snapshot;
}

EzDataStatus ezdataStatus()
{
    return session.status;
}

}  // namespace hal::internal
