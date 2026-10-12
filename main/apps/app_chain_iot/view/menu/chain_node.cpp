/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "chain_node.h"

#include "nodes/angle_node.h"
#include "nodes/captain_node.h"
#include "nodes/empty_node.h"
#include "nodes/joystick_node.h"
#include "nodes/key_node.h"
#include "nodes/rgb_node.h"

#include <algorithm>

namespace chain_iot::view {

std::unique_ptr<ChainNode> makeEmptyNode(bool leftSide)
{
    return std::make_unique<EmptyNode>(leftSide);
}

std::unique_ptr<ChainNode> makeCaptainNode()
{
    return std::make_unique<CaptainNode>();
}

std::unique_ptr<ChainNode> makeChainNode(const ChainDeviceSnapshot& device)
{
    switch (device.kind) {
        case ChainDeviceKind::Key:
            return std::make_unique<KeyNode>(device.id);
        case ChainDeviceKind::Angle:
            return std::make_unique<AngleNode>(device.id);
        case ChainDeviceKind::Joystick:
            return std::make_unique<JoystickNode>(device.id);
        case ChainDeviceKind::Buzzer:
            return std::make_unique<RgbNode>(device.id, NodeKind::Buzzer, "Buzzer");
        case ChainDeviceKind::Encoder:
            return std::make_unique<RgbNode>(device.id, NodeKind::Encoder, "Encoder");
        case ChainDeviceKind::Mono:
            return std::make_unique<RgbNode>(device.id, NodeKind::Mono, "Mono");
        case ChainDeviceKind::Rgb:
            return std::make_unique<RgbNode>(device.id, NodeKind::Rgb, "RGB");
        case ChainDeviceKind::Pir:
            return std::make_unique<RgbNode>(device.id, NodeKind::Pir, "PIR");
        case ChainDeviceKind::Tof:
            return std::make_unique<RgbNode>(device.id, NodeKind::Tof, "ToF");
        default:
            return nullptr;
    }
}

const ChainDeviceSnapshot* findChainDevice(const ChainSnapshot& snapshot, std::uint32_t id)
{
    const auto find_in = [id](const std::vector<ChainDeviceSnapshot>& devices) -> const ChainDeviceSnapshot* {
        const auto found = std::find_if(devices.begin(), devices.end(),
                                        [id](const ChainDeviceSnapshot& device) { return device.id == id; });
        return found == devices.end() ? nullptr : &*found;
    };
    if (const auto* device = find_in(snapshot.left); device != nullptr) return device;
    return find_in(snapshot.right);
}

}  // namespace chain_iot::view
