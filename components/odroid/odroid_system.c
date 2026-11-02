/*
 * Odroid System Compatibility Layer - ESP32-P4, Pico Held 2 v1.4
 *
 * Init order: power hold -> PPA -> USB gamepad -> audio (I2S) -> LCD (DSI).
 * No I2C devices (no touch panel, no audio codec) on this board.
 */

#include "odroid_system.h"
#include "pins_config.h"
#include "st7701_lcd.h"
#include "ppa_engine.h"
#include "gamepad.h"
#include "audio.h"

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "odroid_system";
static bool s_initialized = false;

static void init_power(void)
{
    /* Latch the power switch first: without this the board shuts off as soon
     * as the power button is released. */
    gpio_reset_pin((gpio_num_t)PIN_PWR_EN);
    gpio_set_direction((gpio_num_t)PIN_PWR_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_PWR_EN, 1);

    gpio_reset_pin((gpio_num_t)PIN_CHG_STAT);
    gpio_set_direction((gpio_num_t)PIN_CHG_STAT, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)PIN_CHG_STAT, GPIO_PULLUP_ONLY);

    gpio_set_direction((gpio_num_t)PIN_SD_DET, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)PIN_SD_DET, GPIO_FLOATING);
}

void odroid_system_init(void)
{
    if (s_initialized) return;

    ESP_LOGI(TAG, "=== RetroESP32-P4 System Init (Pico Held 2 v1.4) ===");

    init_power();

    ESP_LOGI(TAG, "Initializing PPA 2D engine...");
    ESP_ERROR_CHECK(ppa_engine_init());

    ESP_LOGI(TAG, "Initializing USB HID gamepad host...");
    gamepad_config_t gp_cfg = GAMEPAD_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(gamepad_init(&gp_cfg));

    ESP_LOGI(TAG, "Initializing audio (I2S)...");
    audio_config_t audio_cfg = AUDIO_CONFIG_DEFAULT();
    audio_cfg.mclk_io     = -1;
    audio_cfg.bclk_io     = PIN_I2S_BCK;
    audio_cfg.ws_io       = PIN_I2S_WS;
    audio_cfg.dout_io     = PIN_I2S_DOUT;
    audio_cfg.din_io      = -1;
    audio_cfg.pa_ctrl_io  = PIN_SND_EN;
    audio_cfg.i2c_handle  = NULL;
    audio_cfg.sample_rate = 16000;
    audio_cfg.volume      = 60;
    esp_err_t audio_ret = audio_init(&audio_cfg);
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed (0x%x), continuing without audio", audio_ret);
    }

    ESP_LOGI(TAG, "Initializing LCD (HD395003C30, 720x720 MIPI DSI)...");
    ESP_ERROR_CHECK(st7701_lcd_init());

    s_initialized = true;
    ESP_LOGI(TAG, "=== System Init Complete ===");
}

/* No I2C bus on this board (kept for API compatibility) */
void *odroid_system_get_i2c_bus(void)
{
    return NULL;
}

void odroid_system_application_set(int slot)
{
    ESP_LOGW(TAG, "Application set to slot %d (stub - emulator launch not yet implemented)", slot);
}

void odroid_system_sleep(void)
{
    ESP_LOGW(TAG, "odroid_system_sleep() called - no deep sleep on P4, doing nothing");
}

void odroid_system_led_set(int value)
{
    (void)value;
}
