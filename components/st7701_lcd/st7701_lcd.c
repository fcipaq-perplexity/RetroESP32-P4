/*
 * LCD driver for the Pico Held 2 v1.4 panel: HD395003C30, 720x720, MIPI DSI.
 *
 * Panel init sequence, DPI timing, reset behaviour and backlight scheme are
 * ported from snes9x_esp32 (components/lcd/lcd_hd395003c30.h, lcd_drv.c).
 * The pixel path uses ESP-IDF's stock DPI panel (one framebuffer in PSRAM,
 * filled via esp_lcd_panel_draw_bitmap / PPA), not the custom line-scan driver.
 *
 * (Legacy st7701 file/function names are kept for source compatibility.)
 */
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"

#include "st7701_lcd.h"
#include "ppa_engine.h"

/* Keep in sync with components/odroid/include/pins_config.h */
#define LCD_H_RES        720
#define LCD_V_RES        720
#define PIN_LCD_RST      23
#define PIN_LCD_BL_EN    7
#define PIN_LCD_BL_PWM   22

#define MIPI_DSI_PHY_PWR_LDO_CHAN       3
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
/* 8-bit duty.  The reference firmware documents 255 as the hardware maximum and
 * 127 as the maximum when running from battery; 100% maps to this value. */
#define BL_DUTY_MAX     127

/* Max time to wait for the previous DMA2D draw to finish */
#define DRAW_WAIT_MS    200

static const char *TAG = "lcd_hd395003c30";

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus = NULL;
static bool s_ready = false;
static bool s_bl_pwm_ready = false;

/* Given whenever no DMA2D draw is in flight */
static SemaphoreHandle_t s_draw_done = NULL;

/* -- Panel init sequence (HD395003C30) ------------------------- */
typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t s_init_cmds[] = {
  {0xB9, (uint8_t []) {0xF1, 0x12, 0x87}, 3, 0},
  {0xB2, (uint8_t []) {0xB4, 0x03, 0X70}, 3, 0},
  {0xB3, (uint8_t []) {0x10, 0x10, 0x28, 0x28, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00}, 10, 0},
  {0xB4, (uint8_t []) {0x80}, 1, 0},
  {0xB5, (uint8_t []) {0x0A, 0x0A}, 2, 0},
  {0xB6, (uint8_t []) {0x8D, 0x8D}, 2, 0},
  {0xB8, (uint8_t []) {0x26, 0x22, 0xF0, 0x13}, 4, 0},
  {0xBA, (uint8_t []) {0x31, 0x81, 0x05, 0xF9, 0x0E, 0x0E, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x25, 0x00, 0x91, 0x0A, 0x00, 0x00, 0x01, 0x4F, 0x01, 0x00, 0x00, 0x37}, 27, 0},
  {0xBC, (uint8_t []) {0x47}, 1, 0},
  {0xBF, (uint8_t []) {0x02, 0x10, 0x00, 0x80, 0x04}, 5, 0},
  {0xC0, (uint8_t []) {0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x12, 0x73, 0x00}, 9, 0},
  {0xC1, (uint8_t []) {0x36, 0x00, 0x32, 0x32, 0x77, 0xE1, 0x77, 0x77, 0xCC, 0xCC, 0xFF, 0xFF, 0x11, 0x11, 0x00, 0x00, 0x32}, 17, 0},
  {0xC7, (uint8_t []) {0x10, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0xED, 0xC5, 0x00, 0xA5}, 12, 0},
  {0xC8, (uint8_t []) {0x10, 0x40, 0x1E, 0x03}, 4, 0},
  {0xCC, (uint8_t []) {0x0B}, 1, 0},
  {0xE0, (uint8_t []) {0x00, 0x0A, 0x0F, 0x2A, 0x33, 0x3F, 0x44, 0x39, 0x06, 0x0C, 0x0E, 0x14, 0x15, 0x13, 0x15, 0x10, 0x18, 0x00, 0x0A, 0x0F, 0x2A, 0x33, 0x3F, 0x44, 0x39, 0x06, 0x0C, 0x0E, 0x14, 0x15, 0x13, 0x15, 0x10, 0x18}, 34, 0},
  {0xE1, (uint8_t []) {0x11, 0x11, 0x91, 0x00, 0x00, 0x00, 0x00}, 7, 0},
  {0xE3, (uint8_t []) {0x07, 0x07, 0x0B, 0x0B, 0x0B, 0x0B, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x04, 0xC0, 0x10}, 14, 0},
  {0xE9, (uint8_t []) {0xC8, 0x10, 0x0A, 0x00, 0x00, 0x80, 0x81, 0x12, 0x31, 0x23, 0x4F, 0x86, 0xA0, 0x00, 0x47, 0x08, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x98, 0x02, 0x8B, 0xAF, 0x46, 0x02, 0x88, 0x88, 0x88, 0x88, 0x88, 0x98, 0x13, 0x8B, 0xAF, 0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 63, 0},
  {0xEA, (uint8_t []) {0x97, 0x0C, 0x09, 0x09, 0x09, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9F, 0x31, 0x8B, 0xA8, 0x31, 0x75, 0x88, 0x88, 0x88, 0x88, 0x88, 0x9F, 0x20, 0x8B, 0xA8, 0x20, 0x64, 0x88, 0x88, 0x88, 0x88, 0x88, 0x23, 0x00, 0x00, 0x02, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x80, 0x81, 0x00, 0x00, 0x00, 0x00}, 61, 0},
  {0xEF, (uint8_t []) {0xFF, 0xFF, 0x01}, 3, 0},
  {0x11, (uint8_t []) {0x00}, 0, 250},   /* sleep out */
  {0x29, (uint8_t []) {0x00}, 0, 50},    /* display on */
};

/* -- Panel wrapper (stock DPI panel + vendor init/reset) ------- */
typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
} hd_panel_t;

static esp_err_t hd_panel_del(esp_lcd_panel_t *panel)
{
    hd_panel_t *hd = (hd_panel_t *)panel->user_data;
    gpio_reset_pin((gpio_num_t)PIN_LCD_RST);
    gpio_set_direction((gpio_num_t)PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 0);
    esp_err_t (*orig_del)(esp_lcd_panel_t *) = hd->del;
    free(hd);
    return orig_del(panel);
}

static esp_err_t hd_panel_reset(esp_lcd_panel_t *panel)
{
    (void)panel;
    /* Reset line has an external pull-up to 1.8 V (IOVCC): pull low, then
     * release as a floating input.  Never drive it high. */
    gpio_reset_pin((gpio_num_t)PIN_LCD_RST);
    gpio_set_direction((gpio_num_t)PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction((gpio_num_t)PIN_LCD_RST, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)PIN_LCD_RST, GPIO_FLOATING);
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}

static esp_err_t hd_panel_init(esp_lcd_panel_t *panel)
{
    hd_panel_t *hd = (hd_panel_t *)panel->user_data;
    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(hd->io, s_init_cmds[i].cmd,
                                                      s_init_cmds[i].data, s_init_cmds[i].data_bytes),
                            TAG, "init command 0x%02X failed", s_init_cmds[i].cmd);
        vTaskDelay(pdMS_TO_TICKS(s_init_cmds[i].delay_ms));
    }
    return hd->init(panel);   /* start the DPI video stream */
}

static esp_err_t new_hd_panel(esp_lcd_panel_io_handle_t io, esp_lcd_dsi_bus_handle_t bus,
                              const esp_lcd_dpi_panel_config_t *dpi_cfg,
                              esp_lcd_panel_handle_t *ret_panel)
{
    hd_panel_t *hd = (hd_panel_t *)calloc(1, sizeof(hd_panel_t));
    ESP_RETURN_ON_FALSE(hd, ESP_ERR_NO_MEM, TAG, "no mem");

    /* Hold the panel in reset until esp_lcd_panel_reset() */
    gpio_reset_pin((gpio_num_t)PIN_LCD_RST);
    gpio_set_direction((gpio_num_t)PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_LCD_RST, 0);

    esp_lcd_panel_handle_t panel = NULL;
    esp_err_t ret = esp_lcd_new_panel_dpi(bus, dpi_cfg, &panel);
    if (ret != ESP_OK) {
        free(hd);
        ESP_LOGE(TAG, "create MIPI DPI panel failed (0x%x)", ret);
        return ret;
    }

    hd->io = io;
    hd->del = panel->del;
    hd->init = panel->init;
    panel->del = hd_panel_del;
    panel->init = hd_panel_init;
    panel->reset = hd_panel_reset;
    panel->user_data = hd;
    *ret_panel = panel;
    return ESP_OK;
}

/* -- Draw synchronisation -------------------------------------- */
/* esp_lcd_panel_draw_bitmap() is asynchronous with DMA2D and refuses to start
 * a new draw while the previous one is still running.  The color-trans-done
 * callback releases a semaphore that every draw waits on. */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_handle_t panel,
                                          esp_lcd_dpi_panel_event_data_t *edata,
                                          void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_draw_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t draw_sync(int x0, int y0, int x1, int y1, const void *data)
{
    if (s_draw_done && xSemaphoreTake(s_draw_done, pdMS_TO_TICKS(DRAW_WAIT_MS)) != pdTRUE)
        ESP_LOGW(TAG, "previous draw did not finish within %d ms", DRAW_WAIT_MS);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, x0, y0, x1, y1, data);
    if (ret != ESP_OK && s_draw_done)
        xSemaphoreGive(s_draw_done);   /* nothing was started */
    return ret;
}

static void draw_wait_idle(void)
{
    if (s_draw_done && xSemaphoreTake(s_draw_done, pdMS_TO_TICKS(DRAW_WAIT_MS)) == pdTRUE)
        xSemaphoreGive(s_draw_done);
}

/* -- Backlight: LEDC PWM on PIN_LCD_BL_PWM + enable line ------- */
static void backlight_pwm_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = BL_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num       = BL_LEDC_TIMER,
        .freq_hz         = 100000,
        .clk_cfg         = LEDC_USE_XTAL_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = PIN_LCD_BL_PWM,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    s_bl_pwm_ready = true;
}

void st7701_lcd_set_backlight(int percent)
{
    if (!s_bl_pwm_ready) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * BL_DUTY_MAX / 100;
    if (percent > 0 && duty == 0) duty = 1;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

/* -- Public API ------------------------------------------------ */
esp_err_t st7701_lcd_init(void)
{
    if (s_ready) return ESP_OK;

    backlight_pwm_init();

    /* MIPI DSI PHY power */
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &ldo), TAG, "DSI PHY LDO failed");

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 600,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus), TAG, "DSI bus failed");

    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_io_handle), TAG, "DBI io failed");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 41,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_pulse_width = 60,
            .hsync_back_porch = 120,
            .hsync_front_porch = 106,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 20,
            .vsync_front_porch = 20,
        },
        .flags = { .use_dma2d = true },
    };
    ESP_RETURN_ON_ERROR(new_hd_panel(s_io_handle, s_dsi_bus, &dpi_cfg, &s_panel_handle), TAG, "panel create failed");

    s_draw_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_draw_done, ESP_ERR_NO_MEM, TAG, "no mem for draw semaphore");
    xSemaphoreGive(s_draw_done);
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_dpi_panel_register_event_callbacks(s_panel_handle, &cbs, NULL),
                        TAG, "register DPI callbacks failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel_handle), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel_handle), TAG, "panel init failed");

    /* Black frame before the backlight comes on */
    st7701_lcd_fill_screen(0x0000);

    gpio_set_direction((gpio_num_t)PIN_LCD_BL_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_LCD_BL_EN, 1);
    s_ready = true;
    st7701_lcd_set_backlight(50);   /* launcher applies the saved brightness afterwards */

    ESP_LOGI(TAG, "LCD initialized (%dx%d, RGB565)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

bool st7701_lcd_is_ready(void)
{
    return s_ready;
}

esp_err_t st7701_lcd_draw_bitmap(uint16_t x_start, uint16_t y_start,
                                  uint16_t x_end, uint16_t y_end,
                                  const uint16_t *color_data)
{
    return draw_sync(x_start, y_start, x_end, y_end, color_data);
}

esp_err_t st7701_lcd_draw_rgb_bitmap(uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h,
                                      const uint16_t *color_data)
{
    return draw_sync(x, y, x + w, y + h, color_data);
}

esp_err_t st7701_lcd_draw_to_fb(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                const uint16_t *data)
{
    draw_wait_idle();

    void *fb = NULL;
    esp_err_t ret = esp_lcd_dpi_panel_get_frame_buffer(s_panel_handle, 1, &fb);
    if (ret != ESP_OK || !fb) return ret ? ret : ESP_ERR_INVALID_STATE;

    uint16_t *fb16 = (uint16_t *)fb;
    for (int row = 0; row < h; row++) {
        memcpy(&fb16[(y + row) * LCD_H_RES + x], &data[row * w], w * sizeof(uint16_t));
    }
    esp_cache_msync(&fb16[y * LCD_H_RES], (size_t)h * LCD_H_RES * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    return ESP_OK;
}

esp_err_t st7701_lcd_fill_screen(uint16_t color)
{
    void *color_data = NULL;
    size_t buf_size = 0;

    esp_err_t ret = ppa_create_filled_buffer(LCD_H_RES, LCD_V_RES, color, &color_data, &buf_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PPA fill failed, falling back to CPU fill");
        buf_size = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
        uint16_t *cpu_buf = (uint16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
        if (!cpu_buf) return ESP_ERR_NO_MEM;
        for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) cpu_buf[i] = color;
        ret = draw_sync(0, 0, LCD_H_RES, LCD_V_RES, cpu_buf);
        draw_wait_idle();   /* the DMA must finish before the buffer is freed */
        heap_caps_free(cpu_buf);
        return ret;
    }

    ret = draw_sync(0, 0, LCD_H_RES, LCD_V_RES, color_data);
    draw_wait_idle();       /* the DMA must finish before the buffer is freed */
    heap_caps_free(color_data);
    return ret;
}

void st7701_lcd_get_handles(bsp_lcd_handles_t *ret_handles)
{
    if (ret_handles) {
        ret_handles->io = s_io_handle;
        ret_handles->mipi_dsi_bus = s_dsi_bus;
        ret_handles->panel = s_panel_handle;
        ret_handles->control = NULL;
    }
}

uint16_t st7701_lcd_width(void)  { return LCD_H_RES; }
uint16_t st7701_lcd_height(void) { return LCD_V_RES; }
