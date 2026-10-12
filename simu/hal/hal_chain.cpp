/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>

namespace {

bool sameTopology(const std::vector<ChainDeviceSnapshot>& left, const std::vector<ChainDeviceSnapshot>& right,
                  const std::vector<ChainDeviceSnapshot>& nextLeft, const std::vector<ChainDeviceSnapshot>& nextRight)
{
    const auto same_side = [](const auto& current, const auto& next) {
        return current.size() == next.size() &&
               std::equal(current.begin(), current.end(), next.begin(),
                          [](const auto& a, const auto& b) { return a.id == b.id && a.kind == b.kind; });
    };
    return same_side(left, nextLeft) && same_side(right, nextRight);
}

}  // namespace

bool Hal::beginChain()
{
    return setPortPowerEnabled(true);
}

void Hal::endChain()
{
    setPortPowerEnabled(false);
}

void Hal::initializeChainFixture()
{
    _chain_left              = {{1, ChainDeviceKind::Key, ChainKeyData{false, RgbColor{0xAB, 0xCD, 0xEF}}}};
    _chain_right             = {{2, ChainDeviceKind::Angle, ChainAngleData{123, RgbColor{0xAB, 0xCD, 0xEF}}}};
    _chain_topology_revision = 1;
}

void Hal::applyChainFixture(std::vector<ChainDeviceSnapshot> left, std::vector<ChainDeviceSnapshot> right)
{
    if (!sameTopology(_chain_left, _chain_right, left, right)) {
        ++_chain_topology_revision;
        if (_chain_topology_revision == 0) _chain_topology_revision = 1;
    }
    _chain_left  = std::move(left);
    _chain_right = std::move(right);
}

ChainSnapshot Hal::getChainSnapshot()
{
    ChainSnapshot snapshot;
    snapshot.capturedAt          = millis();
    snapshot.topologyRevision    = _chain_topology_revision;
    snapshot.left                = _chain_left;
    snapshot.right               = _chain_right;
    snapshot.initialScanComplete = isPortPowerEnabled();
    return snapshot;
}

bool Hal::setChainRgb(std::uint32_t deviceId, RgbColor color)
{
    const auto update_side = [deviceId, color](std::vector<ChainDeviceSnapshot>& devices) {
        const auto device = std::find_if(devices.begin(), devices.end(),
                                         [deviceId](const auto& entry) { return entry.id == deviceId; });
        if (device == devices.end()) return false;
        if (device->kind == ChainDeviceKind::Mono) {
            auto* data = std::get_if<ChainMonoData>(&device->data);
            if (data == nullptr) return false;
            const int luma   = (77 * color.red + 150 * color.green + 29 * color.blue + 128) >> 8;
            const int step   = (luma * 8 + 127) / 255;
            const auto gray  = static_cast<std::uint8_t>((step * 255 + 4) / 8);
            data->rgb        = {gray, gray, gray};
            data->brightness = static_cast<std::uint8_t>(std::max(1, step) - 1);
            data->mode       = 0;
        } else if (device->kind == ChainDeviceKind::Rgb) {
            auto* data = std::get_if<ChainRgbData>(&device->data);
            if (data == nullptr) return false;
            data->rgb        = color;
            data->brightness = 50;
            data->mode       = 0;
        } else {
            std::visit([color](auto& data) { data.rgb = color; }, device->data);
        }
        return true;
    };
    return update_side(_chain_left) || update_side(_chain_right);
}
