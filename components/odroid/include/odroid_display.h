/*
 * Odroid Display Compatibility Layer for ESP32-P4 (Pico Held 2 v1.4, 720x720)
 *
 * Provides the original ILI9341 API surface.  The launcher draws into a
 * 720x480 landscape framebuffer in PSRAM; the PPA rotates it 270 deg and the
 * result is drawn centred on the square 720x720 DSI panel.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Allocate the 720x480 virtual framebuffer (the LCD itself is started by odroid_system_init()). */
void ili9341_init(void);

/** Copy a rectangle of RGB565 pixels into the virtual framebuffer (clipped).
 *  Call display_flush() to push changes to the panel. */
void ili9341_write_frame_rectangleLE(int x, int y, int w, int h, const uint16_t *data);

/** Clear the entire virtual framebuffer to a solid RGB565 color. */
void ili9341_clear(uint16_t color);

/** Flush the framebuffer to the panel (PPA rotate 270 deg, centred). Only if modified. */
void display_flush(void);

/** Force a flush even if the framebuffer has not been modified. */
void display_flush_force(void);

/** Set PPA scale factors used by display_flush() (default 1.0 x 1.0). */
void display_set_scale(float sx, float sy);

/** Set backlight brightness, 0..100 percent. */
void display_set_backlight(int percent);

/** True once the LCD and backlight are running. */
bool is_backlight_initialized(void);

/** Direct pointer to the 720x480 framebuffer (RGB565). */
uint16_t *display_get_framebuffer(void);

/** Pointer to the 320x240 emulator buffer (in-game menus). Draw with a 320-pixel
 *  stride, then call display_emu_flush(). */
uint16_t *display_get_emu_buffer(void);

/** Flush the 320x240 emulator buffer (PPA scale + 270 deg rotate, centred). */
void display_emu_flush(void);

/** Legacy: drew raw pixels at portrait coordinates for the touch sidebar
 *  buttons.  A no-op on this board. */
void display_lcd_draw_raw(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          const uint16_t *data);

/* --- Emulator-specific display write functions ----------------- */

/** Game Boy framebuffer (160x144, direct RGB565). */
void ili9341_write_frame_gb(uint16_t *buffer, int scale);

/** NES framebuffer (256x224, 8-bit palette-indexed, byte-swapped RGB565 palette). */
void ili9341_write_frame_nes(uint8_t *buffer, uint16_t *myPalette, uint8_t scale);

/** SMS/Game Gear framebuffer (indexed). SMS 256x192; Game Gear 160x144 at offset 48. */
void ili9341_write_frame_sms(uint8_t *buffer, uint16_t color[], uint8_t isGameGear, uint8_t scale);

/** C64-style indexed framebuffer (384x272); the centre 320x240 is shown. */
void ili9341_write_frame_c64(uint8_t *buffer, uint16_t *palette);

/** Atari 7800 framebuffer (320x240, 8-bit palette-indexed). */
void ili9341_write_frame_prosystem(uint8_t *buffer, uint16_t *palette);

/** Atari Lynx framebuffer (160x102, RGB565). */
void ili9341_write_frame_lynx(const uint16_t *buffer);

/** Pre-rendered 320x240 RGB565 frame (little-endian byte order). */
void ili9341_write_frame_rgb565(const uint16_t *buffer);

/** Pre-rendered 320x240 RGB565 frame with optional PPA byte swap. */
void ili9341_write_frame_rgb565_ex(const uint16_t *buffer, bool byte_swap_input);

/**
 * Arbitrary-size RGB565 frame, PPA scale + rotate 270 deg, centred on the panel.
 * Sources wider than 720/scale are cropped equally on both sides.  Console-class
 * sources 241..256 px wide (SNES/NES/SMS) are shown at 3x when 2.0 is requested.
 */
void ili9341_write_frame_rgb565_custom(const uint16_t *buffer, uint16_t in_w,
                                        uint16_t in_h, float scale,
                                        bool byte_swap_input);

/** Backlight off (avoids a flash during OTA reboot). */
void ili9341_poweroff(void);

/** Prepare the display for emulator use (no-op on P4). */
void ili9341_prepare(void);

/* --- Display locking (per-emulator mutexes map to a single mutex) - */
void odroid_display_lock(void);
void odroid_display_unlock(void);
void odroid_display_lock_gb_display(void);
void odroid_display_unlock_gb_display(void);
void odroid_display_lock_nes_display(void);
void odroid_display_unlock_nes_display(void);
void odroid_display_lock_sms_display(void);
void odroid_display_unlock_sms_display(void);

/* --- Status screens ------------------------------------------- */
#define ODROID_SD_ERR_NOCARD  0
#define ODROID_SD_ERR_BADFILE 1
void odroid_display_show_sderr(int errNum);
void odroid_display_show_hourglass(void);
void odroid_display_show_splash(void);
void odroid_display_drain_spi(void);

/* Scaling disable keys (used with odroid_settings) */
#define ODROID_SCALE_DISABLE_GB  0
#define ODROID_SCALE_DISABLE_NES 1
#define ODROID_SCALE_DISABLE_SMS 2

#ifdef __cplusplus
}
#endif
