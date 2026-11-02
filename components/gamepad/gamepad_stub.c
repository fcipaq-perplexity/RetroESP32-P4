/*
 * USB gamepad stub - used when the USB CDC console is enabled
 * (CONFIG_TINYUSB_CDC_ENABLED, see launcher/sdkconfig.defaults).
 *
 * The USB OTG port can act as a USB device (console) or a USB host (gamepad),
 * not both.  This stub keeps the gamepad API linkable and reports no gamepad;
 * the built-in buttons are unaffected.
 */
#include <string.h>
#include "gamepad.h"

esp_err_t gamepad_init(const gamepad_config_t *config)
{
    (void)config;
    return ESP_OK;
}

void gamepad_deinit(void)
{
}

void gamepad_get_state(gamepad_state_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool gamepad_is_connected(void)
{
    return false;
}

void gamepad_buttons_to_str(uint32_t buttons, char *buf, size_t buf_size)
{
    (void)buttons;
    if (buf && buf_size) buf[0] = 0;
}

int gamepad_get_raw_report(uint8_t *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    return 0;
}

void gamepad_get_vid_pid(uint16_t *vid, uint16_t *pid)
{
    if (vid) *vid = 0;
    if (pid) *pid = 0;
}
