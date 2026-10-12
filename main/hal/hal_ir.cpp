/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <driver/gpio.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <esp_check.h>
#include <esp_log.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr char Tag[]                      = "HAL-IR";
constexpr gpio_num_t IrTxPin              = GPIO_NUM_48;
constexpr gpio_num_t IrRxPin              = GPIO_NUM_47;
constexpr std::uint32_t Resolution        = 1000000;
constexpr std::uint32_t CarrierHz         = 38000;
constexpr std::size_t NecBitCount         = 32;
constexpr std::size_t MaximumRawSymbols   = (IrMaximumPulseCount + 1) / 2;
constexpr std::size_t RawSymbolCapacity   = MaximumRawSymbols + 1;
constexpr std::uint32_t RawMinPulseNs     = 1250;
constexpr std::uint32_t RawEndOfFrameNs   = 12000000;
constexpr std::uint32_t MaximumPulseUs    = 32767;
constexpr std::uint32_t MinimumCarrierHz  = 20000;
constexpr std::uint32_t MaximumCarrierHz  = 60000;
constexpr std::size_t MinimumCapturePulse = 4;
constexpr std::size_t MaximumStoredSlots  = 16;

constexpr std::array<std::uint8_t, 4> StorageMagic = {'I', 'R', 'C', 'P'};
constexpr std::uint16_t StorageVersion             = 1;
constexpr std::uint16_t StorageHeaderSize          = 24;
constexpr std::size_t StoredPulseSize              = 5;
constexpr std::size_t ChecksumOffset               = 20;

rmt_channel_handle_t tx_channel = nullptr;
rmt_encoder_handle_t tx_encoder = nullptr;
rmt_channel_handle_t rx_channel = nullptr;

std::array<rmt_symbol_word_t, RawSymbolCapacity> rx_symbols = {};
std::atomic<std::size_t> received_symbol_count{0};
std::atomic<bool> receive_done{false};
bool capture_active              = false;
std::uint32_t capture_started_at = 0;
std::uint32_t capture_timeout_ms = 0;

enum class StoredFileResult {
    Missing,
    Valid,
    Invalid,
};

rmt_symbol_word_t makeSymbol(std::uint16_t markUs, std::uint16_t spaceUs)
{
    rmt_symbol_word_t symbol = {};
    symbol.level0            = 1;
    symbol.duration0         = markUs;
    symbol.level1            = 0;
    symbol.duration1         = spaceUs;
    return symbol;
}

bool isValidSignal(const IrRawSignal& signal)
{
    if (signal.carrierFrequency < MinimumCarrierHz || signal.carrierFrequency > MaximumCarrierHz ||
        signal.dutyPermille == 0 || signal.dutyPermille >= 1000 || signal.pulses.empty() ||
        signal.pulses.size() > IrMaximumPulseCount) {
        return false;
    }

    return std::all_of(signal.pulses.begin(), signal.pulses.end(), [](const IrRawPulse& pulse) {
        return pulse.durationUs != 0 && pulse.durationUs <= MaximumPulseUs;
    });
}

bool applyCarrier(std::uint32_t frequency, std::uint16_t dutyPermille)
{
    rmt_carrier_config_t config = {};
    config.frequency_hz         = frequency;
    config.duty_cycle           = static_cast<float>(dutyPermille) / 1000.0f;
    return rmt_apply_carrier(tx_channel, &config) == ESP_OK;
}

bool onReceiveDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* event, void*)
{
    received_symbol_count.store(event->num_symbols, std::memory_order_relaxed);
    receive_done.store(true, std::memory_order_release);
    return false;
}

bool armReceiver()
{
    received_symbol_count.store(0, std::memory_order_relaxed);
    receive_done.store(false, std::memory_order_release);

    rmt_receive_config_t config = {};
    config.signal_range_min_ns  = RawMinPulseNs;
    config.signal_range_max_ns  = RawEndOfFrameNs;
    return rmt_receive(rx_channel, rx_symbols.data(), sizeof(rx_symbols), &config) == ESP_OK;
}

bool restartReceiver()
{
    const esp_err_t disable_result = rmt_disable(rx_channel);
    if (disable_result != ESP_OK) {
        ESP_LOGW(Tag, "failed to stop IR RX: %s", esp_err_to_name(disable_result));
        return false;
    }

    const esp_err_t enable_result = rmt_enable(rx_channel);
    if (enable_result != ESP_OK) {
        ESP_LOGW(Tag, "failed to restart IR RX: %s", esp_err_to_name(enable_result));
        return false;
    }
    return true;
}

void appendPulse(IrRawSignal& signal, bool high, std::uint32_t durationUs)
{
    if (durationUs == 0) {
        return;
    }
    if (!signal.pulses.empty() && signal.pulses.back().high == high &&
        signal.pulses.back().durationUs <= UINT32_MAX - durationUs) {
        signal.pulses.back().durationUs += durationUs;
        return;
    }
    if (signal.pulses.size() < IrMaximumPulseCount) {
        signal.pulses.push_back({high, durationUs});
    }
}

bool decodeCapture(IrRawSignal& signal, std::size_t symbolCount)
{
    if (symbolCount > MaximumRawSymbols) {
        return false;
    }

    signal                  = {};
    signal.carrierFrequency = IrDefaultCarrierFrequency;
    signal.dutyPermille     = IrDefaultDutyPermille;
    signal.pulses.reserve(std::min(symbolCount * 2, IrMaximumPulseCount));

    for (std::size_t index = 0; index < symbolCount && index < rx_symbols.size(); ++index) {
        const auto& symbol = rx_symbols[index];
        appendPulse(signal, symbol.level0 != 0, symbol.duration0);
        appendPulse(signal, symbol.level1 != 0, symbol.duration1);
    }

    while (!signal.pulses.empty() && !signal.pulses.front().high) {
        signal.pulses.erase(signal.pulses.begin());
    }
    while (!signal.pulses.empty() && !signal.pulses.back().high) {
        signal.pulses.pop_back();
    }
    return signal.pulses.size() >= MinimumCapturePulse && isValidSignal(signal);
}

void writeU16(std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t value)
{
    data[offset]     = static_cast<std::uint8_t>(value);
    data[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t byte = 0; byte < 4; ++byte) {
        data[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
    }
}

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8U);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(data[offset + byte]) << (byte * 8U);
    }
    return value;
}

std::uint32_t calculateChecksum(const std::vector<std::uint8_t>& data)
{
    std::uint32_t checksum = UINT32_MAX;
    for (std::size_t index = 0; index < data.size(); ++index) {
        const std::uint8_t value = index >= ChecksumOffset && index < ChecksumOffset + 4 ? 0 : data[index];
        checksum ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (checksum & 1U);
            checksum                 = (checksum >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~checksum;
}

std::vector<std::uint8_t> encodeSignal(const IrRawSignal& signal)
{
    const std::size_t payloadSize = signal.pulses.size() * StoredPulseSize;
    std::vector<std::uint8_t> data(StorageHeaderSize + payloadSize, 0);
    std::copy(StorageMagic.begin(), StorageMagic.end(), data.begin());
    writeU16(data, 4, StorageVersion);
    writeU16(data, 6, StorageHeaderSize);
    writeU32(data, 8, signal.carrierFrequency);
    writeU16(data, 12, signal.dutyPermille);
    writeU16(data, 14, static_cast<std::uint16_t>(signal.pulses.size()));
    writeU32(data, 16, static_cast<std::uint32_t>(payloadSize));

    std::size_t offset = StorageHeaderSize;
    for (const auto& pulse : signal.pulses) {
        writeU32(data, offset, pulse.durationUs);
        data[offset + 4] = pulse.high ? 1 : 0;
        offset += StoredPulseSize;
    }
    writeU32(data, ChecksumOffset, calculateChecksum(data));
    return data;
}

StoredFileResult readSignalFile(const std::string& path, IrRawSignal& signal)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return errno == ENOENT ? StoredFileResult::Missing : StoredFileResult::Invalid;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return StoredFileResult::Invalid;
    }
    const long fileSize = std::ftell(file);
    if (fileSize < static_cast<long>(StorageHeaderSize) ||
        fileSize > static_cast<long>(StorageHeaderSize + IrMaximumPulseCount * StoredPulseSize) ||
        std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return StoredFileResult::Invalid;
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(fileSize));
    const bool readOk = std::fread(data.data(), 1, data.size(), file) == data.size() && std::ferror(file) == 0;
    std::fclose(file);
    if (!readOk || !std::equal(StorageMagic.begin(), StorageMagic.end(), data.begin()) ||
        readU16(data, 4) != StorageVersion || readU16(data, 6) != StorageHeaderSize) {
        return StoredFileResult::Invalid;
    }

    const std::uint16_t pulseCount  = readU16(data, 14);
    const std::uint32_t payloadSize = readU32(data, 16);
    const std::size_t expectedSize  = StorageHeaderSize + static_cast<std::size_t>(pulseCount) * StoredPulseSize;
    if (pulseCount == 0 || pulseCount > IrMaximumPulseCount || payloadSize != pulseCount * StoredPulseSize ||
        data.size() != expectedSize || readU32(data, ChecksumOffset) != calculateChecksum(data)) {
        return StoredFileResult::Invalid;
    }

    IrRawSignal decoded;
    decoded.carrierFrequency = readU32(data, 8);
    decoded.dutyPermille     = readU16(data, 12);
    decoded.pulses.reserve(pulseCount);
    std::size_t offset = StorageHeaderSize;
    for (std::size_t index = 0; index < pulseCount; ++index) {
        const std::uint8_t level = data[offset + 4];
        if (level > 1) {
            return StoredFileResult::Invalid;
        }
        decoded.pulses.push_back({level != 0, readU32(data, offset)});
        offset += StoredPulseSize;
    }
    if (!isValidSignal(decoded)) {
        return StoredFileResult::Invalid;
    }

    signal = std::move(decoded);
    return StoredFileResult::Valid;
}

bool writeSignalFile(const std::string& path, const std::vector<std::uint8_t>& data)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }

    bool success = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    success      = success && std::fflush(file) == 0;
    success      = success && fsync(fileno(file)) == 0;
    success      = std::fclose(file) == 0 && success;
    if (!success) {
        std::remove(path.c_str());
    }
    return success;
}

bool makeSlotPaths(const Hal& hal, std::size_t slot, std::string& finalPath, std::string& temporaryPath,
                   std::string& backupPath)
{
    if (slot >= MaximumStoredSlots) {
        return false;
    }

    char name[24] = {};
    std::snprintf(name, sizeof(name), "ir/slot%u.bin", static_cast<unsigned>(slot));
    finalPath = hal.fileSystemPath(name);
    std::snprintf(name, sizeof(name), "ir/slot%u.tmp", static_cast<unsigned>(slot));
    temporaryPath = hal.fileSystemPath(name);
    std::snprintf(name, sizeof(name), "ir/slot%u.bak", static_cast<unsigned>(slot));
    backupPath = hal.fileSystemPath(name);
    return !finalPath.empty() && !temporaryPath.empty() && !backupPath.empty();
}

}  // namespace

namespace hal::internal {

esp_err_t irInit()
{
    gpio_config_t rx_config = {};
    rx_config.pin_bit_mask  = 1ULL << IrRxPin;
    rx_config.mode          = GPIO_MODE_INPUT;
    rx_config.pull_up_en    = GPIO_PULLUP_DISABLE;
    rx_config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    rx_config.intr_type     = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&rx_config), Tag, "failed to configure IR RX pin");

    rmt_tx_channel_config_t tx_config = {};
    tx_config.gpio_num                = IrTxPin;
    tx_config.clk_src                 = RMT_CLK_SRC_DEFAULT;
    tx_config.resolution_hz           = Resolution;
    tx_config.mem_block_symbols       = 64;
    tx_config.trans_queue_depth       = 1;
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_config, &tx_channel), Tag, "failed to initialize IR TX");

    ESP_RETURN_ON_FALSE(applyCarrier(CarrierHz, IrDefaultDutyPermille), ESP_FAIL, Tag,
                        "failed to configure IR carrier");

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder_config, &tx_encoder), Tag, "failed to create IR encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(tx_channel), Tag, "failed to enable IR TX");
    ESP_LOGI(Tag, "IR initialized with power off");
    return ESP_OK;
}

}  // namespace hal::internal

bool Hal::sendIrNec(std::uint16_t address, std::uint16_t command)
{
    if (!_ir_power_enabled || tx_channel == nullptr || tx_encoder == nullptr ||
        !applyCarrier(IrDefaultCarrierFrequency, IrDefaultDutyPermille)) {
        return false;
    }

    std::array<rmt_symbol_word_t, NecBitCount + 2> symbols = {};
    symbols[0]                                             = makeSymbol(9000, 4500);
    const std::uint32_t payload = static_cast<std::uint32_t>(address) | (static_cast<std::uint32_t>(command) << 16U);
    for (std::size_t bit = 0; bit < NecBitCount; ++bit) {
        symbols[bit + 1] = makeSymbol(560, (payload & (1UL << bit)) != 0 ? 1690 : 560);
    }
    symbols.back() = makeSymbol(560, 560);

    rmt_transmit_config_t config = {};
    config.loop_count            = 0;
    return rmt_transmit(tx_channel, tx_encoder, symbols.data(), symbols.size() * sizeof(symbols[0]), &config) ==
               ESP_OK &&
           rmt_tx_wait_all_done(tx_channel, -1) == ESP_OK;
}

bool Hal::beginIrRaw()
{
    if (!_ir_power_enabled || tx_channel == nullptr || tx_encoder == nullptr) {
        return false;
    }
    if (rx_channel != nullptr) {
        return true;
    }

    rmt_rx_channel_config_t channelConfig = {};
    channelConfig.gpio_num                = IrRxPin;
    channelConfig.clk_src                 = RMT_CLK_SRC_DEFAULT;
    channelConfig.resolution_hz           = Resolution;
    channelConfig.mem_block_symbols       = 64;
    // The board's XL-IRM0038D demodulator is active-low; expose carrier marks as high pulses.
    channelConfig.flags.invert_in = true;
    esp_err_t result              = rmt_new_rx_channel(&channelConfig, &rx_channel);
    if (result != ESP_OK) {
        ESP_LOGE(Tag, "failed to initialize IR RX: %s", esp_err_to_name(result));
        rx_channel = nullptr;
        return false;
    }

    rmt_rx_event_callbacks_t callbacks = {};
    callbacks.on_recv_done             = onReceiveDone;
    result                             = rmt_rx_register_event_callbacks(rx_channel, &callbacks, nullptr);
    if (result == ESP_OK) {
        result = rmt_enable(rx_channel);
    }
    if (result != ESP_OK) {
        ESP_LOGE(Tag, "failed to enable IR RX: %s", esp_err_to_name(result));
        rmt_del_channel(rx_channel);
        rx_channel = nullptr;
        return false;
    }

    ESP_LOGI(Tag, "raw IR ready");
    return true;
}

void Hal::endIrRaw()
{
    if (rx_channel == nullptr) {
        return;
    }

    if (capture_active) {
        capture_active = false;
        receive_done.store(false, std::memory_order_release);
    }
    const esp_err_t disableResult = rmt_disable(rx_channel);
    if (disableResult != ESP_OK) {
        ESP_LOGW(Tag, "failed to disable IR RX: %s", esp_err_to_name(disableResult));
        return;
    }
    const esp_err_t deleteResult = rmt_del_channel(rx_channel);
    if (deleteResult != ESP_OK) {
        ESP_LOGW(Tag, "failed to delete IR RX: %s", esp_err_to_name(deleteResult));
        return;
    }
    rx_channel = nullptr;
    ESP_LOGI(Tag, "raw IR stopped");
}

bool Hal::startIrRawCapture(std::uint32_t timeoutMs)
{
    if (!_ir_power_enabled || rx_channel == nullptr || capture_active || timeoutMs == 0 || !armReceiver()) {
        return false;
    }

    capture_started_at = millis();
    capture_timeout_ms = timeoutMs;
    capture_active     = true;
    return true;
}

IrRawCaptureResult Hal::pollIrRawCapture(IrRawSignal& signal)
{
    if (!_ir_power_enabled || rx_channel == nullptr || !capture_active) {
        return IrRawCaptureResult::Failed;
    }

    const std::uint32_t elapsed = millis() - capture_started_at;
    if (receive_done.load(std::memory_order_acquire)) {
        const std::size_t symbolCount = received_symbol_count.load(std::memory_order_relaxed);
        if (decodeCapture(signal, symbolCount)) {
            capture_active = false;
            ESP_LOGI(Tag, "captured raw signal with %u pulses", static_cast<unsigned>(signal.pulses.size()));
            return IrRawCaptureResult::Success;
        }

        if (elapsed < capture_timeout_ms && armReceiver()) {
            return IrRawCaptureResult::Pending;
        }
        capture_active = false;
        return elapsed >= capture_timeout_ms ? IrRawCaptureResult::Timeout : IrRawCaptureResult::Failed;
    }

    if (elapsed < capture_timeout_ms) {
        return IrRawCaptureResult::Pending;
    }

    capture_active = false;
    receive_done.store(false, std::memory_order_release);
    restartReceiver();
    return IrRawCaptureResult::Timeout;
}

void Hal::cancelIrRawCapture()
{
    if (!capture_active || rx_channel == nullptr) {
        return;
    }

    capture_active = false;
    receive_done.store(false, std::memory_order_release);
    restartReceiver();
}

bool Hal::sendIrRaw(const IrRawSignal& signal)
{
    if (!_ir_power_enabled || rx_channel == nullptr || tx_channel == nullptr || tx_encoder == nullptr ||
        capture_active || !isValidSignal(signal) || !applyCarrier(signal.carrierFrequency, signal.dutyPermille)) {
        return false;
    }

    std::vector<rmt_symbol_word_t> symbols((signal.pulses.size() + 1) / 2);
    for (std::size_t index = 0; index < signal.pulses.size(); ++index) {
        auto& symbol      = symbols[index / 2];
        const auto& pulse = signal.pulses[index];
        if ((index & 1U) == 0) {
            symbol.level0    = pulse.high ? 1 : 0;
            symbol.duration0 = pulse.durationUs;
        } else {
            symbol.level1    = pulse.high ? 1 : 0;
            symbol.duration1 = pulse.durationUs;
        }
    }

    rmt_transmit_config_t config = {};
    config.loop_count            = 0;
    const bool sent =
        rmt_transmit(tx_channel, tx_encoder, symbols.data(), symbols.size() * sizeof(symbols[0]), &config) == ESP_OK &&
        rmt_tx_wait_all_done(tx_channel, -1) == ESP_OK;
    if (sent) {
        ESP_LOGI(Tag, "sent raw signal with %u pulses", static_cast<unsigned>(signal.pulses.size()));
    }
    return sent;
}

bool Hal::loadIrRawSignal(std::size_t slot, IrRawSignal& signal)
{
    if (!isFileSystemReady()) {
        return false;
    }

    std::string finalPath;
    std::string temporaryPath;
    std::string backupPath;
    if (!makeSlotPaths(*this, slot, finalPath, temporaryPath, backupPath)) {
        return false;
    }

    const StoredFileResult finalResult = readSignalFile(finalPath, signal);
    if (finalResult == StoredFileResult::Valid) {
        std::remove(temporaryPath.c_str());
        std::remove(backupPath.c_str());
        return true;
    }

    IrRawSignal backupSignal;
    const StoredFileResult backupResult = readSignalFile(backupPath, backupSignal);
    if (backupResult == StoredFileResult::Valid) {
        std::remove(finalPath.c_str());
        if (std::rename(backupPath.c_str(), finalPath.c_str()) != 0) {
            ESP_LOGW(Tag, "failed to restore IR slot %u backup: errno=%d", static_cast<unsigned>(slot), errno);
        }
        std::remove(temporaryPath.c_str());
        signal = std::move(backupSignal);
        return true;
    }

    if (finalResult == StoredFileResult::Invalid || backupResult == StoredFileResult::Invalid) {
        ESP_LOGW(Tag, "IR slot %u contains invalid data", static_cast<unsigned>(slot));
    }
    std::remove(temporaryPath.c_str());
    return false;
}

bool Hal::saveIrRawSignal(std::size_t slot, const IrRawSignal& signal)
{
    if (!isFileSystemReady() || !isValidSignal(signal) || !ensureFileSystemDirectory("ir")) {
        return false;
    }

    std::string finalPath;
    std::string temporaryPath;
    std::string backupPath;
    if (!makeSlotPaths(*this, slot, finalPath, temporaryPath, backupPath)) {
        return false;
    }

    const auto data = encodeSignal(signal);
    if (!writeSignalFile(temporaryPath, data)) {
        ESP_LOGW(Tag, "failed to write IR slot %u temporary file", static_cast<unsigned>(slot));
        return false;
    }

    if (std::remove(backupPath.c_str()) != 0 && errno != ENOENT) {
        std::remove(temporaryPath.c_str());
        return false;
    }

    bool hadPrevious = false;
    if (std::rename(finalPath.c_str(), backupPath.c_str()) == 0) {
        hadPrevious = true;
    } else if (errno != ENOENT) {
        std::remove(temporaryPath.c_str());
        return false;
    }

    if (std::rename(temporaryPath.c_str(), finalPath.c_str()) != 0) {
        const int installError = errno;
        if (hadPrevious && std::rename(backupPath.c_str(), finalPath.c_str()) != 0) {
            ESP_LOGE(Tag, "failed to restore IR slot %u after write error", static_cast<unsigned>(slot));
        }
        std::remove(temporaryPath.c_str());
        ESP_LOGW(Tag, "failed to install IR slot %u: errno=%d", static_cast<unsigned>(slot), installError);
        return false;
    }

    if (hadPrevious) {
        std::remove(backupPath.c_str());
    }
    ESP_LOGI(Tag, "saved IR slot %u with %u pulses", static_cast<unsigned>(slot),
             static_cast<unsigned>(signal.pulses.size()));
    return true;
}
