/*
 * Console over USB CDC on the ESP32-P4 USB 2.0 OTG (high-speed) pins.
 *
 * Enabled by CONFIG_TINYUSB_CDC_ENABLED (launcher/sdkconfig.defaults).  The PC
 * sees a serial port (e.g. /dev/ttyACM0); open it with idf.py monitor or any
 * terminal program.  ESP-IDF 5.4 + esp_tinyusb 1.x API.
 */
#include "pico_usb_console.h"
#include "sdkconfig.h"

#ifdef CONFIG_TINYUSB_CDC_ENABLED

#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* How long to wait for a terminal to open the port before booting on. */
#define PICO_USB_CONSOLE_WAIT_MS 10000

static const char *TAG = "usb_console";

void pico_usb_console_start(void)
{
    const tinyusb_config_t tusb_cfg = { 0 };   /* defaults: HS port, default descriptors */
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return;
    }

    const tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 64,
        .callback_rx = NULL,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    err = tusb_cdc_acm_init(&acm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tusb_cdc_acm_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_tusb_init_console failed: %s", esp_err_to_name(err));
        return;
    }

    /* Wait for the host to open the port (DTR asserted) */
    for (int waited = 0; waited < PICO_USB_CONSOLE_WAIT_MS && !tud_cdc_connected(); waited += 50)
        vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "USB console active (terminal %s)",
             tud_cdc_connected() ? "connected" : "not connected yet");
}

#else

void pico_usb_console_start(void)
{
}

#endif
