/*
 * Audio driver - plain I2S DAC / amplifier (Pico Held 2), no codec.
 *
 * Volume is applied in software by odroid_audio.c; audio_set_volume() only
 * records the value.  The amplifier enable pin (pa_ctrl_io) is raised once
 * the I2S channel is running.
 */

#include "audio.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx_handle = NULL;
static audio_config_t s_config;
static bool s_initialized = false;

static gpio_num_t pin_or_nc(int io) { return io >= 0 ? (gpio_num_t)io : GPIO_NUM_NC; }

esp_err_t audio_init(const audio_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "no config");
    s_config = *config;

    if (config->pa_ctrl_io >= 0) {
        gpio_reset_pin((gpio_num_t)config->pa_ctrl_io);
        gpio_set_direction((gpio_num_t)config->pa_ctrl_io, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)config->pa_ctrl_io, 0);   /* muted until I2S runs */
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)config->i2s_num, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = pin_or_nc(config->mclk_io),
            .bclk = pin_or_nc(config->bclk_io),
            .ws   = pin_or_nc(config->ws_io),
            .dout = pin_or_nc(config->dout_io),
            .din  = GPIO_NUM_NC,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "TX enable failed");

    if (config->pa_ctrl_io >= 0)
        gpio_set_level((gpio_num_t)config->pa_ctrl_io, 1);

    s_initialized = true;
    ESP_LOGI(TAG, "I2S ready: SR=%d BCLK=%d WS=%d DOUT=%d AMP_EN=%d",
             config->sample_rate, config->bclk_io, config->ws_io,
             config->dout_io, config->pa_ctrl_io);
    return ESP_OK;
}

esp_err_t audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    const int sr = s_config.sample_rate;
    const int chunk_samples = 1024;
    const size_t chunk_bytes = chunk_samples * 2 * sizeof(int16_t);
    int16_t *buf = (int16_t *)heap_caps_malloc(chunk_bytes, MALLOC_CAP_INTERNAL);
    if (!buf) return ESP_ERR_NO_MEM;

    const double phase_inc = 2.0 * M_PI * freq_hz / sr;
    double phase = 0.0;
    const int16_t amplitude = (int16_t)(32000 * volume / 100);
    uint32_t total_samples = (duration_ms > 0) ? (sr * duration_ms / 1000) : 0;
    uint32_t samples_written = 0;
    size_t bytes_written = 0;

    while (duration_ms == 0 || samples_written < total_samples) {
        int n = chunk_samples;
        if (duration_ms > 0 && (total_samples - samples_written) < (uint32_t)chunk_samples)
            n = (int)(total_samples - samples_written);
        for (int i = 0; i < n; i++) {
            int16_t val = (int16_t)(amplitude * sin(phase));
            buf[i * 2] = val;
            buf[i * 2 + 1] = val;
            phase += phase_inc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
        esp_err_t ret = i2s_channel_write(s_tx_handle, buf, n * 2 * sizeof(int16_t),
                                          &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) { heap_caps_free(buf); return ret; }
        samples_written += n;
    }
    heap_caps_free(buf);
    return ESP_OK;
}

esp_err_t audio_stop(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    i2s_channel_disable(s_tx_handle);
    i2s_channel_enable(s_tx_handle);
    return ESP_OK;
}

esp_err_t audio_set_volume(int volume)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_config.volume = volume;   /* software attenuation happens in odroid_audio_submit() */
    return ESP_OK;
}

esp_err_t audio_set_sample_rate(int sample_rate)
{
    if (!s_initialized || !s_tx_handle) return ESP_ERR_INVALID_STATE;
    if (sample_rate == s_config.sample_rate) return ESP_OK;

    /* Cached rate reset by audio_reset_sample_rate(): just cache the new value */
    if (s_config.sample_rate == 0) {
        s_config.sample_rate = sample_rate;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reconfiguring I2S sample rate: %d -> %d", s_config.sample_rate, sample_rate);
    i2s_channel_disable(s_tx_handle);
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t ret = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S clock: %s", esp_err_to_name(ret));
        i2s_channel_enable(s_tx_handle);
        return ret;
    }
    i2s_channel_enable(s_tx_handle);
    s_config.sample_rate = sample_rate;
    return ESP_OK;
}

void audio_reset_sample_rate(void)
{
    s_config.sample_rate = 0;
}

esp_err_t audio_play_pcm(const void *data, size_t len, int sample_rate)
{
    (void)sample_rate;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;

    size_t bytes_written = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    size_t remaining = len;
    while (remaining > 0) {
        size_t to_write = (remaining > 4096) ? 4096 : remaining;
        esp_err_t ret = i2s_channel_write(s_tx_handle, ptr, to_write, &bytes_written, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "PCM write failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ptr += bytes_written;
        remaining -= bytes_written;
    }
    return ESP_OK;
}
