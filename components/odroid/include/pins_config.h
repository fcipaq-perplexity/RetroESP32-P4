#pragma once
/*
 * Pin map - Pico Held 2 v1.4 (ESP32-P4, 720x720 DSI panel, GPIO buttons).
 * Ported from snes9x_esp32 components/engine/hwcfg.h (HW_CONFIG==3).
 */

/* -- LCD ------------------------------------------------------- */
#define LCD_H_RES        720
#define LCD_V_RES        720
#define PIN_LCD_RST      23   /* external pull-up, IOVCC=1.8V: never drive high */
#define PIN_LCD_BL_EN    7
#define PIN_LCD_BL_PWM   22

/* -- Power ----------------------------------------------------- */
#define PIN_PWR_EN       52   /* must be driven high at boot to keep the board powered */
#define PIN_CHG_STAT     53   /* low = on external power / charging */
#define PIN_SD_DET       47
#define PIN_ANA_VBAT     19   /* ADC1, behind a /2 divider */
#define PWR_BAT_MAX_VOL  4050 /* mV */
#define PWR_BAT_MIN_VOL  3400 /* mV */

/* -- Audio: plain I2S DAC + amplifier enable (no codec, no MCLK) -- */
#define PIN_SND_EN       30
#define PIN_I2S_BCK      28
#define PIN_I2S_WS       29
#define PIN_I2S_DOUT     25

/* -- SD card (SDMMC 4-bit) ------------------------------------- */
#define SD_MMC_CLK  43
#define SD_MMC_CMD  44
#define SD_MMC_D0   39
#define SD_MMC_D1   40
#define SD_MMC_D2   41
#define SD_MMC_D3   42

/* -- Buttons (digital, active low, internal pull-up) ----------- */
#define PIN_BTN_A     31
#define PIN_BTN_B     5
#define PIN_BTN_X     32
#define PIN_BTN_Y     54
#define PIN_BTN_LEFT  14
#define PIN_BTN_RGHT  12
#define PIN_BTN_UP    13
#define PIN_BTN_DOWN  15
#define PIN_BTN_SEL   35
#define PIN_BTN_L     11
#define PIN_BTN_R     4
#define PIN_BTN_SW    1    /* -> ODROID_INPUT_MENU */

/* START is an analog input: pressed when the 12 dB ADC reading exceeds the threshold */
#define PIN_BTN_ST            49
#define BTN_ST_ADC_THRESHOLD  3500

/* -- Legacy names kept so unmodified code still compiles ------- */
#define LCD_RST           PIN_LCD_RST
#define LCD_BK_LIGHT_GPIO PIN_LCD_BL_PWM
#define TP_I2C_SDA  -1
#define TP_I2C_SCL  -1
#define TP_RST      -1
#define TP_INT      -1
#define I2S_MCLK_IO -1
#define I2S_BCLK_IO PIN_I2S_BCK
#define I2S_WS_IO   PIN_I2S_WS
#define I2S_DOUT_IO PIN_I2S_DOUT
#define I2S_DIN_IO  -1
#define AUDIO_PA_IO PIN_SND_EN
