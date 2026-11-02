#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Route stdout/stderr (printf + ESP_LOG) to a USB CDC serial device on the
 * USB OTG high-speed port.  Does nothing unless CONFIG_TINYUSB_CDC_ENABLED is
 * set.  Blocks up to PICO_USB_CONSOLE_WAIT_MS for a terminal to open the port
 * so early boot messages are not lost.
 */
void pico_usb_console_start(void);

#ifdef __cplusplus
}
#endif
