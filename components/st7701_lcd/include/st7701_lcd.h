/*
 * LCD component - public header.
 *
 * NOTE: the component/header/function names ("st7701") are legacy.  On this
 * fork they drive the Pico Held 2 v1.4 panel (HD395003C30, 720x720, MIPI DSI,
 * 2 lanes).  Names were kept so the emulator apps that include this header
 * keep building unchanged.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_dsi_bus_handle_t    mipi_dsi_bus;
    esp_lcd_panel_io_handle_t   io;
    esp_lcd_panel_handle_t      panel;
    esp_lcd_panel_handle_t      control;
} bsp_lcd_handles_t;

/** Initialize DSI PHY power, panel (reset + init sequence) and backlight. */
esp_err_t st7701_lcd_init(void);

/** True once st7701_lcd_init() has completed. */
bool st7701_lcd_is_ready(void);

/** Draw a bitmap; x_end/y_end are exclusive. */
esp_err_t st7701_lcd_draw_bitmap(uint16_t x_start, uint16_t y_start,
                                  uint16_t x_end, uint16_t y_end,
                                  const uint16_t *color_data);

/** Draw a 16-bit RGB bitmap at (x,y) with dimensions w x h. */
esp_err_t st7701_lcd_draw_rgb_bitmap(uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h,
                                      const uint16_t *color_data);

/** Fill the screen with a solid color. */
esp_err_t st7701_lcd_fill_screen(uint16_t color);

/** Get the LCD panel handles. */
void st7701_lcd_get_handles(bsp_lcd_handles_t *ret_handles);

/** Panel width / height in pixels (720 x 720). */
uint16_t st7701_lcd_width(void);
uint16_t st7701_lcd_height(void);

/** Write straight into the DPI framebuffer with the CPU (no DMA2D). */
esp_err_t st7701_lcd_draw_to_fb(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                const uint16_t *data);

/** Backlight brightness, 0..100 percent (100 kHz PWM, capped for battery use). */
void st7701_lcd_set_backlight(int percent);

#ifdef __cplusplus
}
#endif
