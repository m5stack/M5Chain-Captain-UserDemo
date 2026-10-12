/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <M5GFX.h>
#include <apps/common/common.h>
#include <assets/assets.h>
#include <M5IOE1.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include <lvgl.h>
#include <cassert>

namespace {

constexpr char Tag[]                    = "HAL-DISP";
constexpr gpio_num_t LcdClockPin        = GPIO_NUM_15;
constexpr gpio_num_t LcdMosiPin         = GPIO_NUM_16;
constexpr gpio_num_t LcdChipSelectPin   = GPIO_NUM_45;
constexpr gpio_num_t LcdDataCommandPin  = GPIO_NUM_46;
constexpr std::uint8_t LcdResetPin      = M5IOE1_PIN_1;
constexpr std::uint8_t LcdPowerPin      = M5IOE1_PIN_12;
constexpr std::uint8_t BacklightPin     = M5IOE1_PIN_11;
constexpr std::uint8_t BacklightPwm     = M5IOE1_PWM_CH3;
constexpr int DisplayWidth              = 240;
constexpr int DisplayHeight             = 240;
constexpr int DisplayRefreshPeriodMs    = 16;
constexpr int LcdWriteFrequencyHz       = 80 * 1000 * 1000;
constexpr std::size_t FrameBufferPixels = DisplayWidth * DisplayHeight;
constexpr std::size_t FrameBufferBytes  = FrameBufferPixels * sizeof(std::uint16_t);

class Jd9853Panel final : public lgfx::Panel_LCD {
protected:
    const std::uint8_t* getInitCommands(std::uint8_t list_number) const override
    {
        static constexpr std::uint8_t InitCommands[] = {
            0x01,
            0 + CMD_INIT_DELAY,
            20,
            0xDF,
            2,
            0x98,
            0x53,
            0xDE,
            1,
            0x00,
            0xB2,
            1,
            0x38,
            0xB7,
            4,
            0x00,
            0x29,
            0x00,
            0x51,
            0xBB,
            6,
            0x28,
            0x2F,
            0x55,
            0x73,
            0x63,
            0xF0,
            0xC0,
            2,
            0x22,
            0xA2,
            0xC1,
            1,
            0x12,
            0xC3,
            8,
            0x7D,
            0x08,
            0x08,
            0x0C,
            0xC2,
            0x72,
            0x22,
            0x88,
            0xC4,
            12,
            0x00,
            0x00,
            0xA0,
            0x79,
            0x0A,
            0x0B,
            0x16,
            0x79,
            0x0A,
            0x0B,
            0x16,
            0x82,
            0xC8,
            32,
            0x3F,
            0x29,
            0x23,
            0x20,
            0x27,
            0x2D,
            0x2A,
            0x2B,
            0x2A,
            0x28,
            0x25,
            0x17,
            0x12,
            0x0D,
            0x07,
            0x02,
            0x3F,
            0x29,
            0x23,
            0x20,
            0x27,
            0x2D,
            0x2A,
            0x2B,
            0x2A,
            0x28,
            0x25,
            0x17,
            0x12,
            0x0D,
            0x07,
            0x02,
            0xD0,
            5,
            0x04,
            0x04,
            0x6A,
            0x1C,
            0x03,
            0xD7,
            2,
            0x00,
            0x20,
            0xE6,
            1,
            0x10,
            0xDE,
            1,
            0x01,
            0xBB,
            1,
            0x04,
            0xD7,
            1,
            0x12,
            0xB7,
            5,
            0x03,
            0x13,
            0xE5,
            0x38,
            0x38,
            0xC1,
            3,
            0x14,
            0x15,
            0xC0,
            0xC2,
            2,
            0x06,
            0x3A,
            0xC4,
            2,
            0x72,
            0x12,
            0xBE,
            1,
            0x00,
            0xDE,
            1,
            0x00,
            0x35,
            1,
            0x00,
            0x36,
            1,
            0x00,
            0x3A,
            1,
            0x05,
            0x2A,
            4,
            0x00,
            0x00,
            0x00,
            0xEF,
            0x2B,
            4,
            0x00,
            0x00,
            0x00,
            0xEF,
            0x11,
            0 + CMD_INIT_DELAY,
            120,
            0x29,
            0 + CMD_INIT_DELAY,
            10,
            0xFF,
            0xFF,
        };
        return list_number == 0 ? InitCommands : nullptr;
    }

    std::uint8_t getColMod(std::uint8_t bits_per_pixel) const override
    {
        return bits_per_pixel > 16 ? 0x06 : 0x05;
    }
};

class ChainCaptainDisplay final : public lgfx::LGFX_Device {
public:
    ChainCaptainDisplay()
    {
        auto bus_config       = _bus.config();
        bus_config.spi_host   = SPI2_HOST;
        bus_config.spi_mode   = 0;
        bus_config.freq_write = LcdWriteFrequencyHz;
        bus_config.freq_read  = 16 * 1000 * 1000;
        bus_config.pin_sclk   = LcdClockPin;
        bus_config.pin_mosi   = LcdMosiPin;
        bus_config.pin_miso   = GPIO_NUM_NC;
        bus_config.pin_dc     = LcdDataCommandPin;
        bus_config.spi_3wire  = true;
        // SPI2 is display-exclusive, and an asynchronous flush may finish from another task.
        bus_config.use_lock = false;
        _bus.config(bus_config);
        _panel.setBus(&_bus);

        auto panel_config            = _panel.config();
        panel_config.pin_cs          = LcdChipSelectPin;
        panel_config.pin_rst         = GPIO_NUM_NC;
        panel_config.pin_busy        = GPIO_NUM_NC;
        panel_config.memory_width    = DisplayWidth;
        panel_config.memory_height   = 320;
        panel_config.panel_width     = DisplayWidth;
        panel_config.panel_height    = DisplayHeight;
        panel_config.offset_x        = 0;
        panel_config.offset_y        = 0;
        panel_config.offset_rotation = 2;
        panel_config.readable        = false;
        panel_config.invert          = false;
        panel_config.rgb_order       = true;
        panel_config.bus_shared      = false;
        _panel.config(panel_config);

        setPanel(&_panel);
    }

    void startPixelsTransfer(const lv_area_t* area, const std::uint8_t* data, std::size_t size)
    {
        waitPixelsTransfer();
        startWrite();
        setWindow(area->x1, area->y1, area->x2, area->y2);

        // The queue path keeps the S3's mandatory 32 KB SPI segments adjacent,
        // without the task delays used by M5GFX's regular large-DMA path.
        _bus.addDMAQueue(data, static_cast<std::uint32_t>(size));
        _bus.execDMAQueue();
        _transfer_pending = true;
    }

    void waitPixelsTransfer()
    {
        if (!_transfer_pending) {
            return;
        }
        waitDMA();
        endWrite();
        _transfer_pending = false;
    }

private:
    Jd9853Panel _panel;
    lgfx::Bus_SPI _bus;
    bool _transfer_pending = false;
};

ChainCaptainDisplay LcdDisplay;
lgfx::LGFX_Sprite LcdCanvas{&LcdDisplay};
lv_display_t* LcdLvglDisplay = nullptr;
void* LcdFrameBuffer         = nullptr;

void freeDisplayBuffers()
{
    LcdCanvas.deleteSprite();
    heap_caps_free(LcdFrameBuffer);
    LcdFrameBuffer = nullptr;
    LcdLvglDisplay = nullptr;
}

bool setIoeOutput(M5IOE1* device, std::uint8_t pin, bool enabled)
{
    m5ioe1_err_t error = M5IOE1_OK;
    device->pinModeWithRes(pin, OUTPUT, &error);
    if (error != M5IOE1_OK || device->setDriveMode(pin, M5IOE1_DRIVE_PUSHPULL) != M5IOE1_OK) {
        return false;
    }
    device->digitalWriteWithRes(pin, enabled ? HIGH : LOW, &error);
    return error == M5IOE1_OK;
}

esp_err_t setDisplayDriveStrength()
{
    constexpr gpio_num_t DisplayPins[] = {LcdClockPin, LcdMosiPin, LcdChipSelectPin, LcdDataCommandPin};
    for (const auto pin : DisplayPins) {
        const auto error = gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

void flushDisplay(lv_display_t* display, const lv_area_t* area, std::uint8_t* color_map)
{
    (void)display;
    if (area == nullptr || color_map == nullptr || area->x1 != 0 || area->y1 != 0 || area->x2 != DisplayWidth - 1 ||
        area->y2 != DisplayHeight - 1) {
        ESP_LOGE(Tag, "LVGL full renderer produced an invalid flush area");
        return;
    }

    const auto pixel_count   = static_cast<std::size_t>(lv_area_get_size(area));
    const auto transfer_size = pixel_count * sizeof(std::uint16_t);
    if (transfer_size > FrameBufferBytes) {
        ESP_LOGE(Tag, "LVGL flush exceeds framebuffer: %zu bytes", transfer_size);
        return;
    }
    LcdDisplay.startPixelsTransfer(area, color_map, transfer_size);
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    GetHAL().recordDisplayFrame();
#endif
}

void waitDisplayFlush(lv_display_t*)
{
    LcdDisplay.waitPixelsTransfer();
}

void wakeDisplayTask(lv_event_t*)
{
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, nullptr);
}

lv_display_t* addLvglDisplay()
{
    LcdFrameBuffer =
        heap_caps_aligned_alloc(CONFIG_LV_DRAW_BUF_ALIGN, FrameBufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (LcdFrameBuffer == nullptr) {
        freeDisplayBuffers();
        ESP_LOGE(Tag, "display framebuffer needs %zu contiguous internal DMA bytes; largest block=%zu",
                 FrameBufferBytes, heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        return nullptr;
    }

    LcdCanvas.setColorDepth(lgfx::color_depth_t::rgb565_2Byte);
    LcdCanvas.setBuffer(LcdFrameBuffer, DisplayWidth, DisplayHeight);
    LcdCanvas.fillScreen(TFT_BLACK);

    lvgl_port_lock(0);
    auto* display = lv_display_create(DisplayWidth, DisplayHeight);
    if (display != nullptr) {
        lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
        lv_display_set_buffers(display, LcdFrameBuffer, nullptr, FrameBufferBytes, LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display, flushDisplay);
        lv_display_set_flush_wait_cb(display, waitDisplayFlush);
        lv_timer_set_period(lv_display_get_refr_timer(display), DisplayRefreshPeriodMs);
        lv_timer_pause(lv_display_get_refr_timer(display));
        lv_display_add_event_cb(display, wakeDisplayTask, LV_EVENT_INVALIDATE_AREA, nullptr);
        lv_display_add_event_cb(display, wakeDisplayTask, LV_EVENT_REFR_REQUEST, nullptr);
    }
    lvgl_port_unlock();

    if (display == nullptr) {
        freeDisplayBuffers();
    } else {
        LcdLvglDisplay = display;
    }
    return display;
}

}  // namespace

namespace hal::internal {

esp_err_t displayBlankBacklight()
{
    auto* ioe_device = ioe();
    if (ioe_device == nullptr || !setIoeOutput(ioe_device, BacklightPin, false) ||
        ioe_device->setPwmDuty(BacklightPwm, 0) != M5IOE1_OK) {
        ESP_LOGE(Tag, "failed to blank backlight before display init");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t displayInit()
{
    auto* ioe_device = ioe();
    if (ioe_device == nullptr || !setIoeOutput(ioe_device, LcdPowerPin, false) ||
        !setIoeOutput(ioe_device, LcdResetPin, false)) {
        ESP_LOGE(Tag, "failed to prepare LCD rails");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!setIoeOutput(ioe_device, LcdPowerPin, true)) {
        ESP_LOGE(Tag, "failed to enable LCD power");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!setIoeOutput(ioe_device, LcdResetPin, true)) {
        ESP_LOGE(Tag, "failed to release LCD reset");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    if (!LcdDisplay.init_without_reset(true)) {
        ESP_LOGE(Tag, "failed to initialize JD9853 through M5GFX");
        return ESP_FAIL;
    }
    if (const auto error = setDisplayDriveStrength(); error != ESP_OK) {
        ESP_LOGE(Tag, "failed to configure LCD GPIO drive strength: %s", esp_err_to_name(error));
        return error;
    }
    LcdDisplay.setRotation(GetHAL().getDisplayRotation());

    lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_config.task_affinity   = 1;
    if (lvgl_port_init(&lvgl_config) != ESP_OK) {
        ESP_LOGE(Tag, "failed to initialize LVGL port");
        return ESP_FAIL;
    }

    if (addLvglDisplay() == nullptr) {
        ESP_LOGE(Tag, "failed to create full-screen LVGL display buffer");
        return ESP_FAIL;
    }

    ESP_LOGI(Tag, "display initialized");
    return ESP_OK;
}

}  // namespace hal::internal

bool Hal::lvglLock()
{
    return lvgl_port_lock(0);
}

void Hal::lvglUnlock()
{
    lvgl_port_unlock();
}

bool Hal::setDisplayRenderer(DisplayRenderer renderer)
{
    if (!_display_initialized || LcdLvglDisplay == nullptr || !lvgl_port_lock(0)) {
        return false;
    }

    if (_display_renderer == renderer) {
        lvgl_port_unlock();
        return true;
    }

    LcdDisplay.waitPixelsTransfer();
    auto* refresh_timer = lv_display_get_refr_timer(LcdLvglDisplay);
    if (renderer == DisplayRenderer::Gfx) {
        lv_timer_pause(refresh_timer);
    } else {
        lv_timer_resume(refresh_timer);
        if (auto* screen = lv_display_get_screen_active(LcdLvglDisplay); screen != nullptr) {
            lv_obj_invalidate(screen);
        }
    }

    _display_renderer = renderer;
    lvgl_port_unlock();

    if (renderer == DisplayRenderer::Lvgl) {
        lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, nullptr);
    }
    return true;
}

lgfx::LGFX_Sprite& Hal::gfxCanvas()
{
    assert(_display_initialized && _display_renderer == DisplayRenderer::Gfx);
    return LcdCanvas;
}

void Hal::presentGfx()
{
    if (!_display_initialized || _display_renderer != DisplayRenderer::Gfx || _gfx_present_suppressed) {
        return;
    }

    static constexpr lv_area_t FullScreenArea = {0, 0, DisplayWidth - 1, DisplayHeight - 1};
    LcdDisplay.startPixelsTransfer(&FullScreenArea, static_cast<const std::uint8_t*>(LcdCanvas.getBuffer()),
                                   FrameBufferBytes);
    LcdDisplay.waitPixelsTransfer();
#if CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG
    GetHAL().recordDisplayFrame();
#endif
}

void Hal::setGfxPresentSuppressed(bool suppressed)
{
    _gfx_present_suppressed = suppressed;
}

bool Hal::setBackLightBrightness(int brightness)
{
    auto* device = hal::internal::ioe();
    if (device == nullptr) {
        return false;
    }
    brightness = brightness < 0 ? 0 : (brightness > 100 ? 100 : brightness);
    if (device->setPwmDuty(BacklightPwm, static_cast<std::uint8_t>(brightness)) != M5IOE1_OK) {
        return false;
    }
    _backlight_brightness = brightness;
    return true;
}

bool Hal::setDisplayRotation(int rotation)
{
    if (!_display_initialized || rotation < 0 || rotation > 3 || !lvgl_port_lock(0)) {
        return false;
    }

    LcdDisplay.waitPixelsTransfer();
    LcdDisplay.setRotation(rotation);
    _display_rotation = rotation;
    if (_display_renderer == DisplayRenderer::Lvgl) {
        if (auto* screen = lv_display_get_screen_active(LcdLvglDisplay); screen != nullptr) {
            lv_obj_invalidate(screen);
        }
    }
    lvgl_port_unlock();
    return true;
}

void Hal::showBootSplash()
{
    if (!_display_initialized || _display_renderer != DisplayRenderer::Gfx) {
        return;
    }

    auto& canvas = gfxCanvas();
    canvas.fillScreen(TFT_BLACK);

    constexpr int LogoX = 25;
    constexpr int LogoY = 72;
    canvas.pushImage(LogoX, LogoY, image_boot_chain_captain_logo.width, image_boot_chain_captain_logo.height,
                     reinterpret_cast<const lgfx::swap565_t*>(image_boot_chain_captain_logo.data));

    const lgfx::LVGLFont version_font{&font_noto_sans_semibold_14};
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::bottom_right);
    canvas.setBaseColor(TFT_BLACK);
    canvas.setTextColor(0xC8C5C5U);
    canvas.setFont(&version_font);
    canvas.drawString(common::FirmwareVersion.data(), 230, 231);

    presentGfx();
}
