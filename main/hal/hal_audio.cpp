/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <M5IOE1.h>
#include <apps/common/audio/audio.h>
#include <audio_codec_if.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_check.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <mutex>
#include <vector>

namespace {

constexpr char Tag[]                   = "HAL-AUDIO";
constexpr std::uint8_t AudioPowerPin   = M5IOE1_PIN_5;
constexpr gpio_num_t AudioMclkPin      = GPIO_NUM_40;
constexpr gpio_num_t AudioBclkPin      = GPIO_NUM_38;
constexpr gpio_num_t AudioWordClockPin = GPIO_NUM_41;
constexpr gpio_num_t AudioDataOutPin   = GPIO_NUM_42;
constexpr gpio_num_t AudioDataInPin    = GPIO_NUM_39;
constexpr gpio_num_t AudioPaPin        = GPIO_NUM_21;
constexpr std::size_t AudioChunkFrames = 256;
constexpr float RecorderInputGainDb    = 12.0F;

i2s_chan_handle_t tx_channel               = nullptr;
i2s_chan_handle_t rx_channel               = nullptr;
const audio_codec_data_if_t* audio_data_if = nullptr;
esp_codec_dev_handle_t codec_device        = nullptr;
std::mutex codec_output_mutex;

class AudioOutput {
public:
    bool init()
    {
        if (_task_handle != nullptr) {
            return true;
        }

        const BaseType_t result = xTaskCreate([](void* context) { static_cast<AudioOutput*>(context)->task(); },
                                              "audio_output", 4 * 1024, this, 5, &_task_handle);
        if (result != pdPASS) {
            _task_handle = nullptr;
            ESP_LOGE(Tag, "failed to create audio output task");
            return false;
        }
        return true;
    }

    void play(const std::vector<std::int16_t>& samples, bool async)
    {
        audio::play_pcm(samples);
        if (!async) {
            while (audio::pcm_playing()) {
                vTaskDelay(1);
            }
        }
    }

    void stop()
    {
        audio::stop_pcm();
    }

    bool writeRaw(const std::int16_t* samples, std::size_t sample_count)
    {
        if (sample_count == 0) {
            return true;
        }
        if (codec_device == nullptr || samples == nullptr) {
            return false;
        }

        std::lock_guard<std::mutex> lock(codec_output_mutex);
        return esp_codec_dev_write(codec_device, const_cast<std::int16_t*>(samples), sample_count * sizeof(*samples)) ==
               ESP_CODEC_DEV_OK;
    }

private:
    void task()
    {
        ESP_LOGI(Tag, "audio output started: %" PRIu32 " Hz, mono, %u frames", GetHAL().getAudioSampleRate(),
                 static_cast<unsigned>(AudioChunkFrames));

        std::array<std::int16_t, AudioChunkFrames> samples{};
        while (true) {
            audio::render(samples.data(), samples.size());
            bool written = false;
            {
                std::lock_guard<std::mutex> lock(codec_output_mutex);
                written = codec_device != nullptr &&
                          esp_codec_dev_write(codec_device, samples.data(), samples.size() * sizeof(samples.front())) ==
                              ESP_CODEC_DEV_OK;
            }
            if (!written) {
                ESP_LOGE(Tag, "audio output failed");
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    TaskHandle_t _task_handle = nullptr;
};

AudioOutput& audioOutput()
{
    static AudioOutput output;
    return output;
}

bool setAudioPower(bool enabled)
{
    auto* device = hal::internal::ioe();
    if (device == nullptr) {
        return false;
    }
    m5ioe1_err_t error = M5IOE1_OK;
    device->pinModeWithRes(AudioPowerPin, OUTPUT, &error);
    if (error != M5IOE1_OK || device->setDriveMode(AudioPowerPin, M5IOE1_DRIVE_PUSHPULL) != M5IOE1_OK) {
        return false;
    }
    device->digitalWriteWithRes(AudioPowerPin, enabled ? HIGH : LOW, &error);
    return error == M5IOE1_OK;
}

void setPaMode()
{
    gpio_set_level(AudioPaPin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    for (int pulse = 0; pulse < 4; ++pulse) {
        gpio_set_level(AudioPaPin, 1);
        esp_rom_delay_us(5);
        gpio_set_level(AudioPaPin, 0);
        esp_rom_delay_us(5);
    }
}

int codecVolumeForLinearGain(int volume)
{
    if (volume <= 0) {
        return 0;
    }

    // esp_codec_dev maps 1..100 linearly from -49.5 dB to 0 dB. Convert the
    // user-facing linear amplitude percentage to that scale so hardware and
    // simulator volume settings have the same perceived progression.
    const double amplitude = static_cast<double>(volume) / 100.0;
    const double decibels  = 20.0 * std::log10(amplitude);
    return std::clamp(static_cast<int>(std::lround(100.0 + decibels * 2.0)), 1, 100);
}

}  // namespace

namespace hal::internal {

esp_err_t audioInit()
{
    if (!setAudioPower(true)) {
        ESP_LOGE(Tag, "failed to enable audio rail");
        return ESP_FAIL;
    }

    gpio_config_t pa_config = {};
    pa_config.pin_bit_mask  = 1ULL << AudioPaPin;
    pa_config.mode          = GPIO_MODE_OUTPUT;
    pa_config.pull_up_en    = GPIO_PULLUP_DISABLE;
    pa_config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    pa_config.intr_type     = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&pa_config), Tag, "failed to configure PA");
    setPaMode();

    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear        = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tx_channel, &rx_channel), Tag,
                        "failed to create I2S channels");

    i2s_std_config_t i2s_config = {};
    i2s_config.clk_cfg          = I2S_STD_CLK_DEFAULT_CONFIG(GetHAL().getAudioSampleRate());
    i2s_config.slot_cfg         = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    i2s_config.gpio_cfg.mclk    = AudioMclkPin;
    i2s_config.gpio_cfg.bclk    = AudioBclkPin;
    i2s_config.gpio_cfg.ws      = AudioWordClockPin;
    i2s_config.gpio_cfg.dout    = AudioDataOutPin;
    i2s_config.gpio_cfg.din     = AudioDataInPin;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_channel, &i2s_config), Tag, "failed to initialize I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_channel, &i2s_config), Tag, "failed to initialize I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_channel), Tag, "failed to enable I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_channel), Tag, "failed to enable I2S RX");

    audio_codec_i2s_cfg_t data_config = {};
    data_config.port                  = I2S_NUM_0;
    data_config.tx_handle             = tx_channel;
    data_config.rx_handle             = rx_channel;
    audio_data_if                     = audio_codec_new_i2s_data(&data_config);
    if (audio_data_if == nullptr) {
        return ESP_FAIL;
    }

    audio_codec_i2c_cfg_t i2c_config        = {};
    i2c_config.port                         = I2C_NUM_0;
    i2c_config.addr                         = ES8311_CODEC_DEFAULT_ADDR;
    i2c_config.bus_handle                   = i2cBus();
    const audio_codec_ctrl_if_t* control_if = audio_codec_new_i2c_ctrl(&i2c_config);
    const audio_codec_gpio_if_t* gpio_if    = audio_codec_new_gpio();
    if (control_if == nullptr || gpio_if == nullptr) {
        return ESP_FAIL;
    }

    esp_codec_dev_hw_gain_t gain     = {};
    gain.pa_voltage                  = 5.0f;
    gain.codec_dac_voltage           = 3.3f;
    es8311_codec_cfg_t codec_config  = {};
    codec_config.ctrl_if             = control_if;
    codec_config.gpio_if             = gpio_if;
    codec_config.codec_mode          = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin              = AudioPaPin;
    codec_config.pa_reverted         = false;
    codec_config.master_mode         = false;
    codec_config.use_mclk            = true;
    codec_config.digital_mic         = false;
    codec_config.hw_gain             = gain;
    const audio_codec_if_t* codec_if = es8311_codec_new(&codec_config);
    if (codec_if == nullptr) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config = {};
    device_config.dev_type            = ESP_CODEC_DEV_TYPE_IN_OUT;
    device_config.codec_if            = codec_if;
    device_config.data_if             = audio_data_if;
    codec_device                      = esp_codec_dev_new(&device_config);
    if (codec_device == nullptr || !GetHAL().setAudioSampleRate(GetHAL().getAudioSampleRate()) ||
        !GetHAL().setSpeakerVolume(GetHAL().getSpeakerVolume()) || !GetHAL().setSpeakerMuted(false)) {
        ESP_LOGE(Tag, "ES8311 initialization failed");
        return ESP_FAIL;
    }

    audio::initialize(GetHAL().getAudioSampleRate());
    // Output gain and mute are handled by ES8311 so the mixer keeps full PCM precision.
    audio::set_output_volume(100);
    audio::set_output_muted(false);
    if (!audioOutput().init()) {
        audio::shutdown();
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(Tag, "audio initialized");
    return ESP_OK;
}

bool audioIsReady()
{
    return codec_device != nullptr;
}

bool audioSetCapturePath(bool capturing)
{
    if (codec_device == nullptr) {
        return false;
    }

    if (capturing) {
        if (esp_codec_dev_set_out_mute(codec_device, true) != ESP_CODEC_DEV_OK ||
            gpio_set_level(AudioPaPin, 0) != ESP_OK) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        return true;
    }

    if (gpio_set_level(AudioPaPin, 1) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    return esp_codec_dev_set_out_mute(codec_device, GetHAL().isSpeakerMuted()) == ESP_CODEC_DEV_OK;
}

}  // namespace hal::internal

bool Hal::setAudioSampleRate(std::uint32_t sampleRate)
{
    if (codec_device == nullptr || sampleRate < 8000 || sampleRate > 48000) {
        return false;
    }
    esp_codec_dev_sample_info_t format = {};
    format.sample_rate                 = sampleRate;
    format.bits_per_sample             = 16;
    format.channel                     = 1;
    std::lock_guard<std::mutex> lock(codec_output_mutex);
    if (esp_codec_dev_close(codec_device) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(codec_device, &format) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_gain(codec_device, RecorderInputGainDb) != ESP_CODEC_DEV_OK) {
        return false;
    }
    _audio_sample_rate = sampleRate;
    return true;
}

bool Hal::setSpeakerVolume(int volume)
{
    if (codec_device == nullptr) {
        return false;
    }
    volume = std::clamp(volume, 0, 100);
    if (esp_codec_dev_set_out_vol(codec_device, codecVolumeForLinearGain(volume)) != ESP_CODEC_DEV_OK) {
        return false;
    }
    _speaker_volume = volume;
    return true;
}

bool Hal::setSpeakerMuted(bool muted)
{
    if (codec_device == nullptr || esp_codec_dev_set_out_mute(codec_device, muted) != ESP_CODEC_DEV_OK) {
        return false;
    }
    _speaker_muted = muted;
    return true;
}

void Hal::audioPlay(std::vector<std::int16_t>& samples, bool async)
{
    audioOutput().play(samples, async);
}

void Hal::audioStop()
{
    audioOutput().stop();
}

bool Hal::audioWrite(const std::int16_t* samples, std::size_t sampleCount)
{
    return audioOutput().writeRaw(samples, sampleCount);
}

bool Hal::audioRead(std::int16_t* samples, std::size_t sampleCount)
{
    if (sampleCount == 0) {
        return true;
    }
    return codec_device != nullptr && samples != nullptr &&
           esp_codec_dev_read(codec_device, samples, sampleCount * sizeof(*samples)) == ESP_CODEC_DEV_OK;
}
