/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <apps/common/debug/display_fps.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lgfx {
inline namespace v1 {
class LGFX_Sprite;
}
}  // namespace lgfx

class Button {
public:
    bool isPressed() const
    {
        return _pressed;
    }
    bool wasPressed() const
    {
        return _was_pressed;
    }
    bool wasReleased() const
    {
        return _was_released;
    }
    bool pressedFor(std::uint32_t ms) const;

private:
    friend class Hal;
    void update(std::uint32_t now, bool pressed);

    bool _pressed                  = false;
    bool _raw_pressed              = false;
    bool _was_pressed              = false;
    bool _was_released             = false;
    std::uint32_t _last_raw_change = 0;
    std::uint32_t _last_change     = 0;
    std::uint32_t _last_update     = 0;
};

struct ImuData {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    float gyroX  = 0.0f;
    float gyroY  = 0.0f;
    float gyroZ  = 0.0f;
};

struct RgbColor {
    std::uint8_t red   = 0;
    std::uint8_t green = 0;
    std::uint8_t blue  = 0;
};

enum class ChainDeviceKind : std::uint8_t {
    Key,
    Angle,
    Joystick,
    Buzzer,
    Encoder,
    Mono,
    Rgb,
    Pir,
    Tof,
};

struct ChainKeyData {
    bool pressed = false;
    RgbColor rgb;
};

struct ChainAngleData {
    std::uint16_t angleDegrees = 0;
    RgbColor rgb;
};

struct ChainJoystickData {
    // Calibrated values in [-127, 127], centered at zero, using the device's native axes.
    std::int16_t x = 0;
    std::int16_t y = 0;
    bool pressed   = false;
    RgbColor rgb;
};

struct ChainBuzzerData {
    RgbColor rgb;
    std::uint16_t frequency = 0;
    std::uint8_t mode       = 0;
    std::uint8_t duty       = 0;
    bool enabled            = false;
};

struct ChainEncoderData {
    RgbColor rgb;
    std::int16_t value = 0;
    std::int16_t delta = 0;
    bool pressed       = false;
};

struct ChainMonoData {
    RgbColor rgb;
    std::uint8_t mode       = 0;
    std::uint8_t brightness = 0;
};

struct ChainRgbData {
    RgbColor rgb;
    std::uint8_t mode       = 0;
    std::uint8_t brightness = 0;
};

struct ChainPirData {
    RgbColor rgb;
    bool detected = false;
};

struct ChainTofData {
    RgbColor rgb;
    std::uint16_t distanceMm = 0;
};

using ChainDeviceData = std::variant<ChainKeyData, ChainAngleData, ChainJoystickData, ChainBuzzerData, ChainEncoderData,
                                     ChainMonoData, ChainRgbData, ChainPirData, ChainTofData>;

struct ChainDeviceSnapshot {
    // Device IDs are stable while a module remains connected and are nonzero;
    // zero is reserved by the app for Captain itself.
    std::uint32_t id     = 0;
    ChainDeviceKind kind = ChainDeviceKind::Key;
    ChainDeviceData data;
};

struct ChainSnapshot {
    std::uint32_t capturedAt       = 0;
    std::uint32_t topologyRevision = 0;
    // Both sides are ordered from the module nearest Captain to the farthest.
    std::vector<ChainDeviceSnapshot> left;
    std::vector<ChainDeviceSnapshot> right;
    // An empty topology is meaningful only after initial discovery finishes.
    bool initialScanComplete = false;
};

enum class EzDataState : std::uint8_t {
    Off,
    WaitingForWifi,
    Registering,
    Connecting,
    Connected,
    Failed,
};

struct EzDataStatus {
    EzDataState state = EzDataState::Off;
    std::string pairingCode;
    std::string lastError;
};

constexpr std::size_t IrMaximumPulseCount         = 512;
constexpr std::uint32_t IrDefaultCarrierFrequency = 38000;
constexpr std::uint16_t IrDefaultDutyPermille     = 333;

struct IrRawPulse {
    bool high                = false;
    std::uint32_t durationUs = 0;
};

struct IrRawSignal {
    std::uint32_t carrierFrequency = IrDefaultCarrierFrequency;
    std::uint16_t dutyPermille     = IrDefaultDutyPermille;
    std::vector<IrRawPulse> pulses;

    bool empty() const
    {
        return pulses.empty();
    }
};

enum class IrRawCaptureResult : std::uint8_t {
    Pending,
    Success,
    Timeout,
    Failed,
};

enum class RecorderCaptureResult : std::uint8_t {
    Recording,
    Complete,
    Failed,
};

enum class RecorderPlaybackResult : std::uint8_t {
    Playing,
    Finished,
    Failed,
};

inline constexpr std::size_t RecorderSlotCount    = 3;
inline constexpr std::uint32_t RecorderMaxSeconds = 15;

inline constexpr std::size_t RecorderWaveformPointCount = 9;
using RecorderWaveform                                  = std::array<std::uint8_t, RecorderWaveformPointCount>;

struct DateTime {
    std::uint16_t year   = 2026;
    std::uint8_t month   = 1;
    std::uint8_t day     = 1;
    std::uint8_t hour    = 0;
    std::uint8_t minute  = 0;
    std::uint8_t second  = 0;
    std::uint8_t weekday = 4;

    bool isValid() const;
};

enum class PowerSource : std::uint8_t {
    Unknown = 0,
    Battery,
    TypeC,
    Chain,
};

enum class WifiSignal : std::uint8_t {
    Off = 0,
    Low,
    Medium,
    High,
    Failed,
};

enum class WifiState : std::uint8_t {
    Off = 0,
    Scanning,
    Connecting,
    Connected,
    Configuring,
    Failed,
};

struct WifiStatus {
    WifiState state   = WifiState::Off;
    WifiSignal signal = WifiSignal::Off;
    int rssi          = 0;
    std::string ssid;
    std::string ipAddress;
    std::string apSsid;
    std::string apWebUrl;
};

enum class DisplayRenderer : std::uint8_t {
    Lvgl,
    Gfx,
};

struct DeviceSettings {
    int backlightBrightness  = 60;
    int displayRotation      = 0;
    std::string screensaver  = "cat-spring";
    int speakerVolume        = 80;
    bool buttonSfxEnabled    = true;
    std::string timezone     = "UTC0";
    bool use24HourClock      = true;
    bool autoTimeSync        = true;
    bool ezDataUploadEnabled = false;
};

class Hal {
public:
    void init();

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms);
    std::uint32_t millis();
    void feedTheDog();

    /* -------------------------------- File system ----------------------------- */
    bool isFileSystemReady() const;
    std::string fileSystemPath(std::string_view relativePath = {}) const;
    std::size_t fileSystemCapacity() const;
    std::size_t fileSystemFreeSpace() const;
    bool ensureFileSystemDirectory(std::string_view relativePath) const;

    const DeviceSettings& deviceSettings() const
    {
        return _device_settings;
    }
    bool saveDeviceSettings(const DeviceSettings& settings);

    /* --------------------------------- Display -------------------------------- */
    bool lvglLock();
    void lvglUnlock();
    bool setDisplayRenderer(DisplayRenderer renderer);
    DisplayRenderer displayRenderer() const
    {
        return _display_renderer;
    }
    lgfx::LGFX_Sprite& gfxCanvas();
    void presentGfx();
    void setGfxPresentSuppressed(bool suppressed);
    void showBootSplash();
    // These are no-ops in normal builds; enable CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG to profile display output.
    void recordDisplayFrame()
    {
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
        _display_fps.recordFrame(_display_renderer == DisplayRenderer::Lvgl);
#endif
    }
    void updateDisplayFps()
    {
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
        _display_fps.update(_display_renderer == DisplayRenderer::Lvgl, millis());
#endif
    }
    bool setBackLightBrightness(int brightness);
    int getBackLightBrightness() const
    {
        return _backlight_brightness;
    }
    bool setDisplayRotation(int rotation);
    int getDisplayRotation() const
    {
        return _display_rotation;
    }

    /* ----------------------------------- RGB ---------------------------------- */
    bool setRgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
    const RgbColor& getRgbColor() const
    {
        return _rgb_color;
    }

    /* ---------------------------------- Chain --------------------------------- */
    bool beginChain();
    void endChain();
    ChainSnapshot getChainSnapshot();
    bool setChainRgb(std::uint32_t deviceId, RgbColor color);

    bool beginChainEzData();
    void endChainEzData();
    EzDataStatus getChainEzDataStatus() const;
    void publishChainEzDataSnapshot(const ChainSnapshot& snapshot);

    /* --------------------------------- Button --------------------------------- */
    Button btnPwr;
    Button btnA;
    Button btnB;
    Button btnC;
    void updateButtonStates();

    /* ----------------------------------- IMU ---------------------------------- */
    bool updateImuData();
    const ImuData& getImuData() const
    {
        return _imu_data;
    }

    /* ----------------------------------- RTC ---------------------------------- */
    bool setTimezone(std::string_view timezone);
    bool getDateTime(DateTime& dateTime);
    bool setDateTime(const DateTime& dateTime);

    /* ---------------------------------- Power --------------------------------- */
    int getBatteryLevel();
    std::uint16_t getBatteryVoltage();
    std::uint16_t getInputVoltage();
    PowerSource getPowerSource();
    bool isBatteryCharging();

    /* ---------------------------------- Wi-Fi --------------------------------- */
    bool startWifiStation();
    bool stopWifiStation();
    bool startWifiConfigAp();
    bool stopWifiConfigAp();
    bool isWifiInitialized() const;
    bool isWifiConfigMode() const;
    WifiSignal getWifiSignal() const;
    bool isWifiConnected() const;
    WifiState getWifiState() const;
    WifiStatus getWifiStatus() const;

    /* ---------------------------------- Power --------------------------------- */
    bool setBatteryChargingEnabled(bool enabled);
    bool isBatteryChargingEnabled();
    bool setPortPowerEnabled(bool enabled);
    bool isPortPowerEnabled() const
    {
        return _port_power_enabled;
    }
    bool setPowerButtonDoubleClickEnabled(bool enabled);
    bool isPowerButtonDoubleClickEnabled() const
    {
        return _power_button_double_click_enabled;
    }
    bool setIrPowerEnabled(bool enabled);
    bool isIrPowerEnabled() const
    {
        return _ir_power_enabled;
    }
    bool sendIrNec(std::uint16_t address, std::uint16_t command);
    bool beginIrRaw();
    void endIrRaw();
    bool startIrRawCapture(std::uint32_t timeoutMs);
    IrRawCaptureResult pollIrRawCapture(IrRawSignal& signal);
    void cancelIrRawCapture();
    bool sendIrRaw(const IrRawSignal& signal);
    bool loadIrRawSignal(std::size_t slot, IrRawSignal& signal);
    bool saveIrRawSignal(std::size_t slot, const IrRawSignal& signal);

    /* ---------------------------------- Audio --------------------------------- */
    bool setAudioSampleRate(std::uint32_t sampleRate);
    std::uint32_t getAudioSampleRate() const
    {
        return _audio_sample_rate;
    }
    bool setSpeakerVolume(int volume);
    int getSpeakerVolume() const
    {
        return _speaker_volume;
    }
    bool setSpeakerMuted(bool muted);
    bool isSpeakerMuted() const
    {
        return _speaker_muted;
    }
    // App-facing playback accepts mono PCM and adapts it to the codec format.
    void audioPlay(std::vector<std::int16_t>& samples, bool async = true);
    void audioStop();
    bool audioWrite(const std::int16_t* samples, std::size_t sampleCount);
    bool audioRead(std::int16_t* samples, std::size_t sampleCount);

    /* -------------------------------- Recorder -------------------------------- */
    bool beginRecorder();
    void endRecorder();
    bool hasRecorderClip(std::size_t slot) const;
    bool startRecorderCapture(std::size_t slot);
    RecorderCaptureResult pollRecorderCapture();
    bool finishRecorderCapture();
    void cancelRecorderCapture();
    bool startRecorderPlayback(std::size_t slot);
    RecorderPlaybackResult pollRecorderPlayback();
    void stopRecorderPlayback();
    RecorderWaveform getRecorderWaveform() const;

private:
    bool initFileSystem();
    bool loadDeviceSettings();

    DeviceSettings _device_settings;
    ImuData _imu_data;
    RgbColor _rgb_color;
    int _backlight_brightness               = 60;
    int _display_rotation                   = 0;
    bool _gfx_present_suppressed            = false;
    int _speaker_volume                     = 80;
    std::uint32_t _audio_sample_rate        = 22050;
    bool _speaker_muted                     = true;
    bool _battery_charging_enabled          = true;
    bool _port_power_enabled                = false;
    bool _power_button_double_click_enabled = true;
    bool _ir_power_enabled                  = false;
    bool _display_initialized               = false;
    bool _file_system_ready                 = false;
    bool _device_settings_writable          = true;
    DisplayRenderer _display_renderer       = DisplayRenderer::Gfx;
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    display_debug::DisplayFpsMonitor _display_fps;
#endif
};

Hal& GetHAL();

class LvglLockGuard {
public:
    LvglLockGuard()
    {
        GetHAL().lvglLock();
    }

    ~LvglLockGuard()
    {
        GetHAL().lvglUnlock();
    }

    LvglLockGuard(const LvglLockGuard&)            = delete;
    LvglLockGuard& operator=(const LvglLockGuard&) = delete;
};
