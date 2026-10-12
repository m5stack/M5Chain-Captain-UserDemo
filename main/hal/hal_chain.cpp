/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>

namespace {

constexpr char Tag[] = "HAL-CHAIN";

constexpr std::uint8_t FrameHeader0 = 0xAA;
constexpr std::uint8_t FrameHeader1 = 0x55;
constexpr std::uint8_t FrameTail0   = 0x55;
constexpr std::uint8_t FrameTail1   = 0xAA;

constexpr std::uint8_t CommandGetRgb               = 0x21;
constexpr std::uint8_t CommandSetRgb               = 0x20;
constexpr std::uint8_t CommandGetAngle12           = 0x30;
constexpr std::uint8_t CommandGetJoystickMapped8   = 0x35;
constexpr std::uint8_t CommandGetUid               = 0xF8;
constexpr std::uint8_t CommandGetType              = 0xFB;
constexpr std::uint8_t CommandGetCount             = 0xFE;
constexpr std::uint8_t CommandGetKeyStatus         = 0xE1;
constexpr std::uint8_t CommandGetBuzzerMode        = 0x31;
constexpr std::uint8_t CommandGetBuzzerFrequency   = 0x34;
constexpr std::uint8_t CommandGetBuzzerDuty        = 0x36;
constexpr std::uint8_t CommandGetBuzzerStatus      = 0x38;
constexpr std::uint8_t CommandGetEncoderValue      = 0x10;
constexpr std::uint8_t CommandGetEncoderDelta      = 0x11;
constexpr std::uint8_t CommandGetPirStatus         = 0x37;
constexpr std::uint8_t CommandGetTofDistance       = 0x50;
constexpr std::uint8_t CommandGetDisplayMode       = 0x11;
constexpr std::uint8_t CommandSetDisplayMode       = 0x10;
constexpr std::uint8_t CommandSetDisplayBuffer     = 0x31;
constexpr std::uint8_t CommandGetDisplayPixel      = 0x32;
constexpr std::uint8_t CommandGetDisplayBuffer     = 0x33;
constexpr std::uint8_t CommandSetDisplayBrightness = 0xE2;
constexpr std::uint8_t CommandGetDisplayBrightness = 0xE3;
constexpr std::uint16_t DeviceTypeAngle            = 0x0002;
constexpr std::uint16_t DeviceTypeKey              = 0x0003;
constexpr std::uint16_t DeviceTypeJoystick         = 0x0004;
constexpr std::uint16_t DeviceTypeEncoder          = 0x0001;
constexpr std::uint16_t DeviceTypeTof              = 0x0005;
constexpr std::uint16_t DeviceTypePir              = 0x0009;
constexpr std::uint16_t DeviceTypeBuzzer           = 0x000B;
// BID values from the current Chain Mono and Chain RGB protocol documents.
constexpr std::uint16_t DeviceTypeMono = 0x000D;
constexpr std::uint16_t DeviceTypeRgb  = 0x000E;

constexpr std::size_t MaximumDeviceCount = 16;
constexpr std::size_t MaximumPacketSize  = 137;  // 64 RGB565 pixels plus framing.
// The RGB module recommends staying below 60% for sustained output.
constexpr std::uint8_t RgbDisplayBrightness = 50;
constexpr std::uint32_t CommandTimeoutMs    = 35;
constexpr std::uint32_t ScanIntervalMs      = 400;
constexpr std::uint32_t PollIntervalMs      = 60;
constexpr std::uint32_t RgbIntervalMs       = 500;
constexpr std::uint32_t BasicIntervalMs     = 500;
// Modules need 200 ms in the bootloader and 170 ms for downstream discovery.
constexpr std::uint32_t StartupDelayMs = 400;
constexpr std::uint8_t ScanMissLimit   = 2;
constexpr std::uint16_t AngleRange     = 280;

bool isSupportedDeviceType(std::uint16_t type)
{
    return type == DeviceTypeKey || type == DeviceTypeAngle || type == DeviceTypeJoystick ||
           type == DeviceTypeEncoder || type == DeviceTypeTof || type == DeviceTypePir || type == DeviceTypeBuzzer ||
           type == DeviceTypeMono || type == DeviceTypeRgb;
}

bool isRgbLedDeviceType(std::uint16_t type)
{
    return isSupportedDeviceType(type) && type != DeviceTypeMono && type != DeviceTypeRgb;
}

bool isRgbDeviceKind(ChainDeviceKind kind)
{
    return kind == ChainDeviceKind::Key || kind == ChainDeviceKind::Angle || kind == ChainDeviceKind::Joystick ||
           kind == ChainDeviceKind::Encoder || kind == ChainDeviceKind::Tof || kind == ChainDeviceKind::Pir ||
           kind == ChainDeviceKind::Buzzer || kind == ChainDeviceKind::Mono || kind == ChainDeviceKind::Rgb;
}

struct ChainBus {
    const char* name;
    uart_port_t port;
    gpio_num_t rxPin;
    gpio_num_t txPin;
    bool installed            = false;
    std::uint8_t scanMisses   = 0;
    std::uint8_t lastIndex    = 0;
    std::uint8_t lastCommand  = 0;
    std::size_t receivedBytes = 0;

    struct Node {
        std::uint8_t chainIndex   = 0;
        std::uint16_t type        = 0;
        std::uint32_t uid         = 0;
        ChainDeviceData data      = ChainKeyData{};
        std::uint32_t rgbReadAt   = 0;
        std::uint32_t basicReadAt = 0;
    };
    std::vector<Node> nodes{};
};

// PORT.A is on Captain's left; PORT.B is on its right.
ChainBus leftBus{"left/Port A", UART_NUM_1, GPIO_NUM_6, GPIO_NUM_7};
ChainBus rightBus{"right/Port B", UART_NUM_2, GPIO_NUM_17, GPIO_NUM_18};

struct RgbWrite {
    std::uint32_t deviceId;
    RgbColor color;
};

SemaphoreHandle_t snapshotMutex = nullptr;
SemaphoreHandle_t taskStopped   = nullptr;
TaskHandle_t chainTask          = nullptr;
std::atomic<bool> chainRunning{false};
ChainSnapshot publishedSnapshot;
std::vector<RgbWrite> pendingRgbWrites;

std::uint32_t nowMs()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

bool deadlineReached(std::uint32_t now, std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

bool waitWhileRunning(std::uint32_t durationMs)
{
    const std::uint32_t deadline = nowMs() + durationMs;
    while (chainRunning.load(std::memory_order_relaxed)) {
        const std::uint32_t now = nowMs();
        if (deadlineReached(now, deadline)) return true;
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(std::min<std::uint32_t>(10, deadline - now))));
    }
    return false;
}

std::uint8_t checksum(const std::uint8_t* data, std::size_t size)
{
    std::uint8_t value = 0;
    for (std::size_t i = 0; i < size; ++i) value = static_cast<std::uint8_t>(value + data[i]);
    return value;
}

bool readResponse(ChainBus& bus, std::uint8_t expectedIndex, std::uint8_t expectedCommand,
                  std::vector<std::uint8_t>& response)
{
    std::array<std::uint8_t, MaximumPacketSize> buffer{};
    std::size_t buffered          = 0;
    const std::int64_t deadlineUs = esp_timer_get_time() + CommandTimeoutMs * 1000;

    while (chainRunning.load(std::memory_order_relaxed) && esp_timer_get_time() < deadlineUs) {
        const std::int64_t remainingUs = deadlineUs - esp_timer_get_time();
        const TickType_t waitTicks     = std::max<TickType_t>(1, pdMS_TO_TICKS((remainingUs + 999) / 1000));
        std::size_t available          = 0;
        uart_get_buffered_data_len(bus.port, &available);
        const std::size_t readSize = available == 0 ? 1 : std::min(available, buffer.size() - buffered);
        // A large blocking read waits out its timeout even after a complete short response arrives.
        const int read = uart_read_bytes(bus.port, buffer.data() + buffered, readSize, available == 0 ? waitTicks : 0);
        if (read <= 0) continue;
        buffered += static_cast<std::size_t>(read);
        bus.receivedBytes += static_cast<std::size_t>(read);

        while (buffered >= 2) {
            std::size_t header = 0;
            while (header + 1 < buffered && (buffer[header] != FrameHeader0 || buffer[header + 1] != FrameHeader1)) {
                ++header;
            }
            if (header != 0) {
                std::memmove(buffer.data(), buffer.data() + header, buffered - header);
                buffered -= header;
            }
            if (buffered < 4) break;

            const std::size_t declared   = buffer[2] | (static_cast<std::size_t>(buffer[3]) << 8U);
            const std::size_t packetSize = declared + 6;
            if (declared < 3 || packetSize > buffer.size()) {
                std::memmove(buffer.data(), buffer.data() + 1, --buffered);
                continue;
            }
            if (buffered < packetSize) break;

            const bool validTail = buffer[packetSize - 2] == FrameTail0 && buffer[packetSize - 1] == FrameTail1;
            const std::uint8_t receivedChecksum = buffer[packetSize - 3];
            const bool validChecksum            = receivedChecksum == checksum(buffer.data() + 4, declared - 1);
            const std::uint8_t index            = buffer[4];
            const std::uint8_t command          = buffer[5];
            if (validTail && validChecksum && index == expectedIndex && command == expectedCommand) {
                response.assign(buffer.begin() + 6, buffer.begin() + packetSize - 3);
                return true;
            }

            std::memmove(buffer.data(), buffer.data() + packetSize, buffered - packetSize);
            buffered -= packetSize;
        }

        if (buffered == buffer.size()) buffered = 0;
    }
    return false;
}

bool transact(ChainBus& bus, std::uint8_t index, std::uint8_t command, const std::uint8_t* payload,
              std::size_t payloadSize, std::vector<std::uint8_t>& response)
{
    if (!bus.installed || payloadSize + 9 > MaximumPacketSize) return false;
    bus.lastIndex     = index;
    bus.lastCommand   = command;
    bus.receivedBytes = 0;

    std::array<std::uint8_t, MaximumPacketSize> packet{};
    const std::size_t packetSize = payloadSize + 9;
    const std::uint16_t declared = static_cast<std::uint16_t>(payloadSize + 3);
    packet[0]                    = FrameHeader0;
    packet[1]                    = FrameHeader1;
    packet[2]                    = static_cast<std::uint8_t>(declared);
    packet[3]                    = static_cast<std::uint8_t>(declared >> 8U);
    packet[4]                    = index;
    packet[5]                    = command;
    if (payload != nullptr && payloadSize != 0) std::memcpy(packet.data() + 6, payload, payloadSize);
    packet[6 + payloadSize] = checksum(packet.data() + 4, payloadSize + 2);
    packet[7 + payloadSize] = FrameTail0;
    packet[8 + payloadSize] = FrameTail1;

    uart_flush_input(bus.port);
    if (uart_write_bytes(bus.port, packet.data(), packetSize) != static_cast<int>(packetSize) ||
        uart_wait_tx_done(bus.port, pdMS_TO_TICKS(CommandTimeoutMs)) != ESP_OK) {
        return false;
    }
    return readResponse(bus, index, command, response);
}

bool query(ChainBus& bus, std::uint8_t index, std::uint8_t command, std::vector<std::uint8_t>& response)
{
    return transact(bus, index, command, nullptr, 0, response);
}

bool queryUid(ChainBus& bus, std::uint8_t index, std::uint32_t& uid)
{
    const std::uint8_t uidType = 0;
    std::vector<std::uint8_t> response;
    if (!transact(bus, index, CommandGetUid, &uidType, 1, response) || response.size() < 5 || response[0] != 1) {
        return false;
    }

    uid = static_cast<std::uint32_t>(response[1]) | (static_cast<std::uint32_t>(response[2]) << 8U) |
          (static_cast<std::uint32_t>(response[3]) << 16U) | (static_cast<std::uint32_t>(response[4]) << 24U);
    return uid != 0;
}

bool matchesCachedNode(ChainBus& bus, const ChainBus::Node& node)
{
    if (node.uid != 0) {
        std::uint32_t uid = 0;
        return queryUid(bus, node.chainIndex, uid) && uid == node.uid;
    }

    std::vector<std::uint8_t> response;
    if (!query(bus, node.chainIndex, CommandGetType, response) || response.size() < 2) return false;
    const std::uint16_t type = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
    return type == node.type;
}

bool scanBus(ChainBus& bus, std::vector<ChainBus::Node>& scanned)
{
    const std::uint8_t initialCount = 0;
    std::vector<std::uint8_t> response;
    if (!transact(bus, 0xFF, CommandGetCount, &initialCount, 1, response) || response.empty()) return false;

    const std::size_t count = std::min<std::size_t>(response[0], MaximumDeviceCount);
    scanned.clear();
    scanned.reserve(count);
    for (std::size_t offset = 0; offset < count; ++offset) {
        const auto index = static_cast<std::uint8_t>(offset + 1);
        if (!query(bus, index, CommandGetType, response) || response.size() < 2) return false;

        const std::uint16_t type = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
        ChainBus::Node node;
        node.chainIndex = index;
        node.type       = type;
        if (isSupportedDeviceType(type)) {
            if (!queryUid(bus, index, node.uid)) return false;
            const auto previous = std::find_if(bus.nodes.begin(), bus.nodes.end(),
                                               [&](const auto& item) { return item.uid == node.uid; });
            if (previous != bus.nodes.end()) {
                node.data        = previous->data;
                node.rgbReadAt   = previous->rgbReadAt;
                node.basicReadAt = previous->basicReadAt;
            } else if (type == DeviceTypeAngle) {
                node.data = ChainAngleData{};
            } else if (type == DeviceTypeJoystick) {
                node.data = ChainJoystickData{};
            } else if (type == DeviceTypeBuzzer) {
                node.data = ChainBuzzerData{};
            } else if (type == DeviceTypeEncoder) {
                node.data = ChainEncoderData{};
            } else if (type == DeviceTypeMono) {
                node.data = ChainMonoData{};
            } else if (type == DeviceTypeRgb) {
                node.data = ChainRgbData{};
            } else if (type == DeviceTypePir) {
                node.data = ChainPirData{};
            } else if (type == DeviceTypeTof) {
                node.data = ChainTofData{};
            }
        }
        scanned.push_back(std::move(node));
    }
    return true;
}

bool sameVisibleTopology(const ChainSnapshot& current, const ChainSnapshot& next)
{
    const auto sameSide = [](const auto& lhs, const auto& rhs) {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                          [](const auto& a, const auto& b) { return a.id == b.id && a.kind == b.kind; });
    };
    return sameSide(current.left, next.left) && sameSide(current.right, next.right);
}

void appendVisibleNodes(const ChainBus& bus, std::vector<ChainDeviceSnapshot>& output)
{
    for (const auto& node : bus.nodes) {
        if (node.type == DeviceTypeKey) {
            output.push_back({node.uid, ChainDeviceKind::Key, node.data});
        } else if (node.type == DeviceTypeAngle) {
            output.push_back({node.uid, ChainDeviceKind::Angle, node.data});
        } else if (node.type == DeviceTypeJoystick) {
            output.push_back({node.uid, ChainDeviceKind::Joystick, node.data});
        } else if (node.type == DeviceTypeEncoder) {
            output.push_back({node.uid, ChainDeviceKind::Encoder, node.data});
        } else if (node.type == DeviceTypeTof) {
            output.push_back({node.uid, ChainDeviceKind::Tof, node.data});
        } else if (node.type == DeviceTypePir) {
            output.push_back({node.uid, ChainDeviceKind::Pir, node.data});
        } else if (node.type == DeviceTypeBuzzer) {
            output.push_back({node.uid, ChainDeviceKind::Buzzer, node.data});
        } else if (node.type == DeviceTypeMono) {
            output.push_back({node.uid, ChainDeviceKind::Mono, node.data});
        } else if (node.type == DeviceTypeRgb) {
            output.push_back({node.uid, ChainDeviceKind::Rgb, node.data});
        }
    }
}

void applyNodeColor(ChainDeviceData& data, RgbColor color)
{
    if (auto* mono = std::get_if<ChainMonoData>(&data)) {
        const int luminance = (77 * color.red + 150 * color.green + 29 * color.blue + 128) >> 8;
        const int step      = (luminance * 8 + 127) / 255;
        const auto gray     = static_cast<std::uint8_t>((step * 255 + 4) / 8);
        mono->rgb           = {gray, gray, gray};
        mono->mode          = 0;
        // Level 0 is the lowest lit level; Off is a cleared framebuffer.
        mono->brightness = static_cast<std::uint8_t>(std::max(1, step) - 1);
    } else if (auto* rgb = std::get_if<ChainRgbData>(&data)) {
        rgb->rgb        = color;
        rgb->mode       = 0;
        rgb->brightness = RgbDisplayBrightness;
    } else {
        std::visit([color](auto& value) { value.rgb = color; }, data);
    }
}

bool writeNodeCommand(ChainBus& bus, const ChainBus::Node& node, std::uint8_t command, const std::uint8_t* payload,
                      std::size_t size)
{
    std::vector<std::uint8_t> response;
    if (transact(bus, node.chainIndex, command, payload, size, response) && response.size() == 1 && response[0] == 1) {
        return true;
    }
    ESP_LOGW(Tag, "%s index=%u command=0x%02X failed (status=%d)", bus.name, node.chainIndex, command,
             response.empty() ? -1 : response[0]);
    return false;
}

bool writeDisplayColor(ChainBus& bus, const ChainBus::Node& node, const ChainDeviceData& data)
{
    const auto* mono = std::get_if<ChainMonoData>(&data);
    const auto* rgb  = std::get_if<ChainRgbData>(&data);
    if (mono == nullptr && rgb == nullptr) return false;

    const std::uint8_t mode         = 0;
    const std::uint8_t brightness[] = {mono != nullptr ? mono->brightness : rgb->brightness, 0};
    if (!writeNodeCommand(bus, node, CommandSetDisplayMode, &mode, sizeof(mode)) ||
        !writeNodeCommand(bus, node, CommandSetDisplayBrightness, brightness, sizeof(brightness))) {
        return false;
    }

    if (mono != nullptr) {
        std::array<std::uint8_t, 8> pixels{};
        pixels.fill(mono->rgb.red == 0 ? 0x00 : 0xFF);
        return writeNodeCommand(bus, node, CommandSetDisplayBuffer, pixels.data(), pixels.size());
    }

    const auto color          = rgb->rgb;
    const std::uint16_t pixel = ((color.red >> 3U) << 11U) | ((color.green >> 2U) << 5U) | (color.blue >> 3U);
    std::array<std::uint8_t, 128> pixels{};
    for (std::size_t i = 0; i < pixels.size(); i += 2) {
        pixels[i]     = static_cast<std::uint8_t>(pixel);
        pixels[i + 1] = static_cast<std::uint8_t>(pixel >> 8U);
    }
    return writeNodeCommand(bus, node, CommandSetDisplayBuffer, pixels.data(), pixels.size());
}

bool updateSnapshotRgb(ChainSnapshot& snapshot, const RgbWrite& request)
{
    for (auto* side : {&snapshot.left, &snapshot.right}) {
        const auto node =
            std::find_if(side->begin(), side->end(), [&](const auto& entry) { return entry.id == request.deviceId; });
        if (node == side->end()) continue;
        if (!isRgbDeviceKind(node->kind)) {
            continue;
        }
        applyNodeColor(node->data, request.color);
        return true;
    }
    return false;
}

void applyRgbWrites()
{
    std::vector<RgbWrite> requests;
    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    requests.swap(pendingRgbWrites);
    xSemaphoreGive(snapshotMutex);

    for (const auto& request : requests) {
        if (!chainRunning.load(std::memory_order_relaxed)) return;
        for (ChainBus* bus : {&leftBus, &rightBus}) {
            const auto node = std::find_if(bus->nodes.begin(), bus->nodes.end(),
                                           [&](const auto& entry) { return entry.uid == request.deviceId; });
            if (node == bus->nodes.end()) continue;
            if (!isSupportedDeviceType(node->type)) {
                continue;
            }
            if (!matchesCachedNode(*bus, *node)) break;

            if (node->type == DeviceTypeMono || node->type == DeviceTypeRgb) {
                auto data = node->data;
                applyNodeColor(data, request.color);
                if (writeDisplayColor(*bus, *node, data)) {
                    node->data = std::move(data);
                }
                break;
            }

            const auto color = request.color;
            // Key exposes a single RGB color and applies it to both LEDs.
            const std::uint8_t payload[] = {0, 1, color.red, color.green, color.blue};
            std::vector<std::uint8_t> response;
            if (transact(*bus, node->chainIndex, CommandSetRgb, payload, sizeof(payload), response) &&
                !response.empty() && response[0] == 1) {
                std::visit([color](auto& data) { data.rgb = color; }, node->data);
                node->rgbReadAt = nowMs();
            }
            break;
        }
    }
}

void publish()
{
    ChainSnapshot next;
    next.capturedAt          = nowMs();
    next.initialScanComplete = true;
    appendVisibleNodes(leftBus, next.left);
    appendVisibleNodes(rightBus, next.right);

    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    for (const auto& request : pendingRgbWrites) updateSnapshotRgb(next, request);
    next.topologyRevision = publishedSnapshot.topologyRevision;
    if (!sameVisibleTopology(publishedSnapshot, next)) {
        ++next.topologyRevision;
        if (next.topologyRevision == 0) next.topologyRevision = 1;
        ESP_LOGI(Tag, "topology revision %lu: left=%u right=%u", static_cast<unsigned long>(next.topologyRevision),
                 static_cast<unsigned>(next.left.size()), static_cast<unsigned>(next.right.size()));
    }
    publishedSnapshot = std::move(next);
    xSemaphoreGive(snapshotMutex);
}

void updateTopology(ChainBus& bus)
{
    std::vector<ChainBus::Node> scanned;
    if (scanBus(bus, scanned)) {
        bus.scanMisses = 0;
        bus.nodes      = std::move(scanned);
        return;
    }

    if (bus.scanMisses < ScanMissLimit && ++bus.scanMisses == ScanMissLimit) {
        ESP_LOGI(Tag, "%s scan: no valid response (TX=%d RX=%d index=%u cmd=0x%02X received=%u bytes)", bus.name,
                 static_cast<int>(bus.txPin), static_cast<int>(bus.rxPin), bus.lastIndex, bus.lastCommand,
                 static_cast<unsigned>(bus.receivedBytes));
    }
    if (bus.scanMisses < ScanMissLimit) return;

    // After unplugging downstream, modules can still answer directly while their tail detection settles.
    const auto unavailable = std::find_if(bus.nodes.begin(), bus.nodes.end(),
                                          [&](const auto& node) { return !matchesCachedNode(bus, node); });
    bus.nodes.erase(unavailable, bus.nodes.end());
}

void pollBus(ChainBus& bus, std::uint32_t now)
{
    std::vector<std::uint8_t> response;
    for (auto& node : bus.nodes) {
        if (!chainRunning.load(std::memory_order_relaxed)) return;

        if (node.type == DeviceTypeKey) {
            auto* key = std::get_if<ChainKeyData>(&node.data);
            if (key == nullptr) {
                node.data = ChainKeyData{};
                key       = std::get_if<ChainKeyData>(&node.data);
            }
            if (query(bus, node.chainIndex, CommandGetKeyStatus, response) && !response.empty()) {
                key->pressed = response[0] != 0;
            }
        } else if (node.type == DeviceTypeAngle) {
            auto* angle = std::get_if<ChainAngleData>(&node.data);
            if (angle == nullptr) {
                node.data = ChainAngleData{};
                angle     = std::get_if<ChainAngleData>(&node.data);
            }
            if (query(bus, node.chainIndex, CommandGetAngle12, response) && response.size() >= 2) {
                const std::uint16_t adc = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
                angle->angleDegrees =
                    static_cast<std::uint16_t>((std::min<std::uint16_t>(adc, 4095) * AngleRange + 2047) / 4095);
            }
        } else if (node.type == DeviceTypeJoystick) {
            auto* joystick = std::get_if<ChainJoystickData>(&node.data);
            if (joystick == nullptr) {
                node.data = ChainJoystickData{};
                joystick  = std::get_if<ChainJoystickData>(&node.data);
            }
            if (query(bus, node.chainIndex, CommandGetJoystickMapped8, response) && response.size() >= 2) {
                const auto decodeAxis = [](std::uint8_t value) {
                    const std::int16_t signedValue = value < 128 ? value : static_cast<int>(value) - 256;
                    return std::max<std::int16_t>(-127, signedValue);
                };
                joystick->x = decodeAxis(response[0]);
                joystick->y = decodeAxis(response[1]);
            }
            if (query(bus, node.chainIndex, CommandGetKeyStatus, response) && !response.empty()) {
                joystick->pressed = response[0] != 0;
            }
        } else if (node.type == DeviceTypeEncoder) {
            auto* encoder = std::get_if<ChainEncoderData>(&node.data);
            if (encoder == nullptr) {
                node.data = ChainEncoderData{};
                encoder   = std::get_if<ChainEncoderData>(&node.data);
            }
            if (query(bus, node.chainIndex, CommandGetKeyStatus, response) && response.size() == 1) {
                encoder->pressed = response[0] == 1;
            }
        } else if (!isSupportedDeviceType(node.type)) {
            continue;
        }
        if (deadlineReached(now, node.basicReadAt + BasicIntervalMs)) {
            if (node.type == DeviceTypeBuzzer) {
                auto* buzzer = std::get_if<ChainBuzzerData>(&node.data);
                if (buzzer == nullptr) {
                    node.data = ChainBuzzerData{};
                    buzzer    = std::get_if<ChainBuzzerData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetBuzzerMode, response) && !response.empty()) {
                    buzzer->mode = response[0];
                }
                if (query(bus, node.chainIndex, CommandGetBuzzerFrequency, response) && response.size() >= 2) {
                    buzzer->frequency = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
                }
                if (query(bus, node.chainIndex, CommandGetBuzzerDuty, response) && !response.empty()) {
                    buzzer->duty = response[0];
                }
                if (query(bus, node.chainIndex, CommandGetBuzzerStatus, response) && !response.empty()) {
                    buzzer->enabled = response[0] != 0;
                }
            } else if (node.type == DeviceTypeEncoder) {
                auto* encoder = std::get_if<ChainEncoderData>(&node.data);
                if (encoder == nullptr) {
                    node.data = ChainEncoderData{};
                    encoder   = std::get_if<ChainEncoderData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetEncoderValue, response) && response.size() >= 2) {
                    const auto value =
                        static_cast<std::uint16_t>(response[0]) | (static_cast<std::uint16_t>(response[1]) << 8U);
                    encoder->value = static_cast<std::int16_t>(value);
                }
                if (query(bus, node.chainIndex, CommandGetEncoderDelta, response) && response.size() >= 2) {
                    const auto delta =
                        static_cast<std::uint16_t>(response[0]) | (static_cast<std::uint16_t>(response[1]) << 8U);
                    encoder->delta = static_cast<std::int16_t>(delta);
                }
            } else if (node.type == DeviceTypePir) {
                auto* pir = std::get_if<ChainPirData>(&node.data);
                if (pir == nullptr) {
                    node.data = ChainPirData{};
                    pir       = std::get_if<ChainPirData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetPirStatus, response) && !response.empty()) {
                    pir->detected = response[0] != 0;
                }
            } else if (node.type == DeviceTypeTof) {
                auto* tof = std::get_if<ChainTofData>(&node.data);
                if (tof == nullptr) {
                    node.data = ChainTofData{};
                    tof       = std::get_if<ChainTofData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetTofDistance, response) && response.size() >= 2) {
                    tof->distanceMm = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
                }
            } else if (node.type == DeviceTypeMono) {
                auto* mono = std::get_if<ChainMonoData>(&node.data);
                if (mono == nullptr) {
                    node.data = ChainMonoData{};
                    mono      = std::get_if<ChainMonoData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetDisplayMode, response) && !response.empty()) {
                    mono->mode = response[0];
                }
                if (query(bus, node.chainIndex, CommandGetDisplayBrightness, response) && response.size() == 1 &&
                    response[0] <= 7) {
                    mono->brightness = response[0];
                }
                if (query(bus, node.chainIndex, CommandGetDisplayBuffer, response) && response.size() == 8) {
                    const bool lit  = std::any_of(response.begin(), response.end(), [](auto row) { return row != 0; });
                    const auto gray = static_cast<std::uint8_t>(lit ? ((mono->brightness + 1) * 255 + 4) / 8 : 0);
                    mono->rgb       = {gray, gray, gray};
                }
            } else if (node.type == DeviceTypeRgb) {
                auto* rgb = std::get_if<ChainRgbData>(&node.data);
                if (rgb == nullptr) {
                    node.data = ChainRgbData{};
                    rgb       = std::get_if<ChainRgbData>(&node.data);
                }
                if (query(bus, node.chainIndex, CommandGetDisplayMode, response) && !response.empty()) {
                    rgb->mode = response[0];
                }
                if (query(bus, node.chainIndex, CommandGetDisplayBrightness, response) && !response.empty()) {
                    rgb->brightness = response[0];
                }
                // The color control fills the matrix uniformly, so one pixel is enough for readback.
                const std::uint8_t pixel[] = {1, 0};
                if (transact(bus, node.chainIndex, CommandGetDisplayPixel, pixel, sizeof(pixel), response) &&
                    response.size() == 2) {
                    const std::uint16_t color = response[0] | (static_cast<std::uint16_t>(response[1]) << 8U);
                    rgb->rgb                  = {static_cast<std::uint8_t>((((color >> 11U) & 31U) * 255U + 15U) / 31U),
                                                 static_cast<std::uint8_t>((((color >> 5U) & 63U) * 255U + 31U) / 63U),
                                                 static_cast<std::uint8_t>(((color & 31U) * 255U + 15U) / 31U)};
                }
            }
            node.basicReadAt = now;
        }
        if (isRgbLedDeviceType(node.type) && deadlineReached(now, node.rgbReadAt + RgbIntervalMs)) {
            const std::uint8_t request[] = {0, 1};
            if (transact(bus, node.chainIndex, CommandGetRgb, request, sizeof(request), response) &&
                response.size() >= 4 && response[0] == 1) {
                std::visit([&](auto& data) { data.rgb = {response[1], response[2], response[3]}; }, node.data);
            }
            node.rgbReadAt = now;
        }
    }
}

esp_err_t initializeBus(ChainBus& bus)
{
    uart_config_t config = {};
    config.baud_rate     = 115200;
    config.data_bits     = UART_DATA_8_BITS;
    config.parity        = UART_PARITY_DISABLE;
    config.stop_bits     = UART_STOP_BITS_1;
    config.flow_ctrl     = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk    = UART_SCLK_DEFAULT;

    esp_err_t result = uart_driver_install(bus.port, 512, 0, 0, nullptr, 0);
    if (result == ESP_OK) result = uart_param_config(bus.port, &config);
    if (result == ESP_OK) result = uart_set_pin(bus.port, bus.txPin, bus.rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        uart_driver_delete(bus.port);
        ESP_LOGE(Tag, "%s UART initialization failed: %s", bus.name, esp_err_to_name(result));
        return result;
    }
    bus.installed = true;
    uart_flush_input(bus.port);
    ESP_LOGI(Tag, "%s UART ready (TX=%d RX=%d)", bus.name, static_cast<int>(bus.txPin), static_cast<int>(bus.rxPin));
    return ESP_OK;
}

void shutdownBus(ChainBus& bus)
{
    bus.nodes.clear();
    bus.scanMisses = 0;
    if (bus.installed) uart_driver_delete(bus.port);
    bus.installed = false;
}

void chainWorker(void*)
{
    std::uint32_t nextScan = nowMs();
    bool initialScan       = true;
    waitWhileRunning(StartupDelayMs);
    while (chainRunning.load(std::memory_order_relaxed)) {
        const std::uint32_t now = nowMs();
        if (deadlineReached(now, nextScan)) {
            updateTopology(leftBus);
            if (!chainRunning.load(std::memory_order_relaxed)) break;
            updateTopology(rightBus);
            if (initialScan && (leftBus.scanMisses != 0 || rightBus.scanMisses != 0)) {
                if (!waitWhileRunning(PollIntervalMs)) break;
                if (leftBus.scanMisses != 0) updateTopology(leftBus);
                if (!chainRunning.load(std::memory_order_relaxed)) break;
                if (rightBus.scanMisses != 0) updateTopology(rightBus);
            }
            nextScan = nowMs() + ScanIntervalMs;
        }
        if (!chainRunning.load(std::memory_order_relaxed)) break;
        applyRgbWrites();
        pollBus(leftBus, now);
        pollBus(rightBus, now);
        if (!chainRunning.load(std::memory_order_relaxed)) break;
        publish();
        initialScan = false;
        vTaskDelay(pdMS_TO_TICKS(PollIntervalMs));
    }

    chainTask = nullptr;
    xSemaphoreGive(taskStopped);
    vTaskDelete(nullptr);
}

}  // namespace

bool Hal::beginChain()
{
    if (chainRunning.load(std::memory_order_relaxed)) return true;

    if (snapshotMutex == nullptr) snapshotMutex = xSemaphoreCreateMutex();
    if (taskStopped == nullptr) taskStopped = xSemaphoreCreateBinary();
    if (snapshotMutex == nullptr || taskStopped == nullptr) {
        ESP_LOGE(Tag, "failed to allocate synchronization primitives");
        return false;
    }
    xSemaphoreTake(taskStopped, 0);

    if (!setPortPowerEnabled(true)) {
        ESP_LOGE(Tag, "failed to enable Chain port power");
        return false;
    }
    if (initializeBus(leftBus) != ESP_OK || initializeBus(rightBus) != ESP_OK) {
        shutdownBus(leftBus);
        shutdownBus(rightBus);
        setPortPowerEnabled(false);
        return false;
    }

    chainRunning.store(true, std::memory_order_relaxed);
    if (xTaskCreate(chainWorker, "chain_iot", 6144, nullptr, 4, &chainTask) != pdPASS) {
        chainRunning.store(false, std::memory_order_relaxed);
        shutdownBus(leftBus);
        shutdownBus(rightBus);
        setPortPowerEnabled(false);
        ESP_LOGE(Tag, "failed to create Chain worker task");
        return false;
    }
    return true;
}

void Hal::endChain()
{
    if (chainRunning.exchange(false, std::memory_order_relaxed)) {
        const TaskHandle_t worker = chainTask;
        if (worker != nullptr && xSemaphoreTake(taskStopped, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(Tag, "Chain worker did not stop in time");
            vTaskDelete(worker);
            chainTask = nullptr;
        }
    }

    shutdownBus(leftBus);
    shutdownBus(rightBus);
    if (snapshotMutex != nullptr) {
        xSemaphoreTake(snapshotMutex, portMAX_DELAY);
        pendingRgbWrites.clear();
        if (!publishedSnapshot.left.empty() || !publishedSnapshot.right.empty()) {
            ++publishedSnapshot.topologyRevision;
            if (publishedSnapshot.topologyRevision == 0) publishedSnapshot.topologyRevision = 1;
        }
        publishedSnapshot.left.clear();
        publishedSnapshot.right.clear();
        publishedSnapshot.capturedAt          = nowMs();
        publishedSnapshot.initialScanComplete = false;
        xSemaphoreGive(snapshotMutex);
    }
    setPortPowerEnabled(false);
}

ChainSnapshot Hal::getChainSnapshot()
{
    ChainSnapshot snapshot;
    if (snapshotMutex != nullptr) {
        xSemaphoreTake(snapshotMutex, portMAX_DELAY);
        snapshot = publishedSnapshot;
        xSemaphoreGive(snapshotMutex);
    }
    snapshot.capturedAt = millis();
    return snapshot;
}

bool Hal::setChainRgb(std::uint32_t deviceId, RgbColor color)
{
    if (!chainRunning.load(std::memory_order_relaxed) || snapshotMutex == nullptr) return false;

    xSemaphoreTake(snapshotMutex, portMAX_DELAY);
    const RgbWrite request{deviceId, color};
    const bool accepted = updateSnapshotRgb(publishedSnapshot, request);
    if (accepted) {
        // Keep UART and node access in the worker; coalesce rapid color changes per device.
        const auto pending = std::find_if(pendingRgbWrites.begin(), pendingRgbWrites.end(),
                                          [deviceId](const auto& entry) { return entry.deviceId == deviceId; });
        if (pending == pendingRgbWrites.end()) {
            pendingRgbWrites.push_back(request);
        } else {
            pending->color = color;
        }
    }
    xSemaphoreGive(snapshotMutex);
    return accepted;
}
