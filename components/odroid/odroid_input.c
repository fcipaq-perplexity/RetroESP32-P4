/*
 * Odroid Input Compatibility Layer - ESP32-P4, Pico Held 2 v1.4
 *
 * Inputs (OR-ed together):
 *   - Built-in GPIO buttons (d-pad, A/B/X/Y, L/R, SELECT, START, SW)
 *   - Optional USB HID gamepad
 *
 *   BTN_SW  -> ODROID_INPUT_MENU   (opens the in-game menu)
 *   START   -> analog input on GPIO 49 (pressed when ADC > BTN_ST_ADC_THRESHOLD)
 *   VOLUME  -> no dedicated input yet (Y still maps to VOLUME on emulators
 *              that leave odroid_input_xy_menu_disable cleared)
 *
 * No touch panel exists on this board.
 */

#include "odroid_input.h"
#include "gamepad.h"
#include "pins_config.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "odroid_input";
static bool s_initialized = false;

volatile int odroid_paddle_adc_raw = -1;     /* no paddle on this board */
bool odroid_input_xy_menu_disable = false;
bool odroid_input_touch_buttons_disable = false;   /* unused: no touch panel */

/* --- Built-in buttons ----------------------------------------- */
typedef struct { int pin; int input; } pad_button_t;

static const pad_button_t k_pad_buttons[] = {
    { PIN_BTN_UP,    ODROID_INPUT_UP     },
    { PIN_BTN_DOWN,  ODROID_INPUT_DOWN   },
    { PIN_BTN_LEFT,  ODROID_INPUT_LEFT   },
    { PIN_BTN_RGHT,  ODROID_INPUT_RIGHT  },
    { PIN_BTN_A,     ODROID_INPUT_A      },
    { PIN_BTN_B,     ODROID_INPUT_B      },
    { PIN_BTN_X,     ODROID_INPUT_X      },
    { PIN_BTN_Y,     ODROID_INPUT_Y      },
    { PIN_BTN_L,     ODROID_INPUT_L      },
    { PIN_BTN_R,     ODROID_INPUT_R      },
    { PIN_BTN_SEL,   ODROID_INPUT_SELECT },
    { PIN_BTN_SW,    ODROID_INPUT_MENU   },
};
#define PAD_BUTTON_COUNT (sizeof(k_pad_buttons) / sizeof(k_pad_buttons[0]))

/* --- ADC helpers (START button, battery) ---------------------- */
static adc_oneshot_unit_handle_t s_adc_unit[2] = { NULL, NULL };

static bool adc_pin_setup(int gpio, adc_unit_t *unit, adc_channel_t *chan)
{
    if (adc_oneshot_io_to_channel(gpio, unit, chan) != ESP_OK) {
        ESP_LOGW(TAG, "GPIO %d is not an ADC pin", gpio);
        return false;
    }
    if (!s_adc_unit[*unit]) {
        adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = *unit };
        if (adc_oneshot_new_unit(&unit_cfg, &s_adc_unit[*unit]) != ESP_OK) {
            ESP_LOGW(TAG, "ADC unit %d init failed", (int)*unit);
            return false;
        }
    }
    adc_oneshot_chan_cfg_t chan_cfg = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    return adc_oneshot_config_channel(s_adc_unit[*unit], *chan, &chan_cfg) == ESP_OK;
}

static adc_unit_t    s_start_unit;
static adc_channel_t s_start_chan;
static bool          s_start_ok = false;

static adc_unit_t    s_bat_unit;
static adc_channel_t s_bat_chan;
static bool          s_bat_ok = false;
static adc_cali_handle_t s_bat_cali = NULL;

static void pad_gpio_init(void)
{
    for (size_t i = 0; i < PAD_BUTTON_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << k_pad_buttons[i].pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }
    s_start_ok = adc_pin_setup(PIN_BTN_ST, &s_start_unit, &s_start_chan);
    ESP_LOGI(TAG, "GPIO buttons ready (START via ADC: %s)", s_start_ok ? "ok" : "unavailable");
}

static bool start_pressed(void)
{
    if (!s_start_ok) return false;
    int raw = 0, sum = 0, n = 0;
    adc_oneshot_read(s_adc_unit[s_start_unit], s_start_chan, &raw);   /* discard first conversion */
    for (int i = 0; i < 4; i++) {
        if (adc_oneshot_read(s_adc_unit[s_start_unit], s_start_chan, &raw) == ESP_OK) { sum += raw; n++; }
    }
    return n > 0 && (sum / n) > BTN_ST_ADC_THRESHOLD;
}

static void pad_read(odroid_gamepad_state *state)
{
    for (size_t i = 0; i < PAD_BUTTON_COUNT; i++) {
        if (gpio_get_level((gpio_num_t)k_pad_buttons[i].pin) == 0)
            state->values[k_pad_buttons[i].input] = 1;
    }
    if (start_pressed())
        state->values[ODROID_INPUT_START] = 1;
}

/* --- USB Gamepad Button Mapping ------------------------------- */
/* Index: 0=A, 1=B, 2=X, 3=Y, 4=L, 5=R, 6=SELECT, 7=START */
static odroid_usb_map_t s_usb_map = {
    .btn = {
        GAMEPAD_BTN_A,
        GAMEPAD_BTN_B,
        GAMEPAD_BTN_X,
        GAMEPAD_BTN_Y,
        GAMEPAD_BTN_L1 | GAMEPAD_BTN_L2,
        GAMEPAD_BTN_R1 | GAMEPAD_BTN_R2,
        GAMEPAD_BTN_SELECT,
        GAMEPAD_BTN_START,
    }
};
static bool s_usb_map_loaded = false;
static uint16_t s_usb_map_vid = 0;
static uint16_t s_usb_map_pid = 0;

void odroid_input_gamepad_init(void)
{
    if (s_initialized) return;
    pad_gpio_init();
    /* USB gamepad is initialized in odroid_system_init() */
    s_initialized = true;
    ESP_LOGI(TAG, "Input subsystem ready (GPIO buttons + USB HID gamepad)");
}

void odroid_input_gamepad_read(odroid_gamepad_state *state)
{
    memset(state, 0, sizeof(odroid_gamepad_state));

    gamepad_state_t gp;
    gamepad_get_state(&gp);

    if (gp.connected) {
        uint16_t vid = 0, pid = 0;
        gamepad_get_vid_pid(&vid, &pid);
        if (!s_usb_map_loaded || vid != s_usb_map_vid || pid != s_usb_map_pid) {
            odroid_input_usb_map_load();
        }

        state->values[ODROID_INPUT_UP]    = (gp.dpad & GAMEPAD_DPAD_UP)    ? 1 : 0;
        state->values[ODROID_INPUT_DOWN]  = (gp.dpad & GAMEPAD_DPAD_DOWN)  ? 1 : 0;
        state->values[ODROID_INPUT_LEFT]  = (gp.dpad & GAMEPAD_DPAD_LEFT)  ? 1 : 0;
        state->values[ODROID_INPUT_RIGHT] = (gp.dpad & GAMEPAD_DPAD_RIGHT) ? 1 : 0;

        if (gp.axis_ly < -64) state->values[ODROID_INPUT_UP]    = 1;
        if (gp.axis_ly >  64) state->values[ODROID_INPUT_DOWN]  = 1;
        if (gp.axis_lx < -64) state->values[ODROID_INPUT_LEFT]  = 1;
        if (gp.axis_lx >  64) state->values[ODROID_INPUT_RIGHT] = 1;
        if (s_usb_map.btn[8]  && (gp.buttons & s_usb_map.btn[8]))  state->values[ODROID_INPUT_UP]    = 1;
        if (s_usb_map.btn[9]  && (gp.buttons & s_usb_map.btn[9]))  state->values[ODROID_INPUT_DOWN]  = 1;
        if (s_usb_map.btn[10] && (gp.buttons & s_usb_map.btn[10])) state->values[ODROID_INPUT_LEFT]  = 1;
        if (s_usb_map.btn[11] && (gp.buttons & s_usb_map.btn[11])) state->values[ODROID_INPUT_RIGHT] = 1;

        state->values[ODROID_INPUT_A]      = (gp.buttons & s_usb_map.btn[0]) ? 1 : 0;
        state->values[ODROID_INPUT_B]      = (gp.buttons & s_usb_map.btn[1]) ? 1 : 0;
        state->values[ODROID_INPUT_X]      = (gp.buttons & s_usb_map.btn[2]) ? 1 : 0;
        state->values[ODROID_INPUT_Y]      = (gp.buttons & s_usb_map.btn[3]) ? 1 : 0;
        state->values[ODROID_INPUT_L]      = (gp.buttons & s_usb_map.btn[4]) ? 1 : 0;
        state->values[ODROID_INPUT_R]      = (gp.buttons & s_usb_map.btn[5]) ? 1 : 0;
        state->values[ODROID_INPUT_SELECT] = (gp.buttons & s_usb_map.btn[6]) ? 1 : 0;
        state->values[ODROID_INPUT_START]  = (gp.buttons & s_usb_map.btn[7]) ? 1 : 0;

        /* Same behaviour as the HDMI build: L2 -> MENU on a USB pad */
        if (gp.buttons & GAMEPAD_BTN_L2) state->values[ODROID_INPUT_MENU]   = 1;
        if (gp.buttons & GAMEPAD_BTN_R2) state->values[ODROID_INPUT_VOLUME] = 1;
    }

    /* Built-in buttons (OR-ed in) */
    pad_read(state);

    /* X -> Menu, Y -> Volume for emulators that lack native X/Y (skipped for SNES/Genesis). */
    if (!odroid_input_xy_menu_disable) {
        state->values[ODROID_INPUT_MENU]   |= state->values[ODROID_INPUT_X];
        state->values[ODROID_INPUT_VOLUME] |= state->values[ODROID_INPUT_Y];
    }
}

odroid_gamepad_state odroid_input_read_raw(void)
{
    odroid_gamepad_state state;
    odroid_input_gamepad_read(&state);
    return state;
}

void odroid_paddle_adc_init(void)
{
    ESP_LOGI(TAG, "No analog paddle on this board");
}

/* --- Battery (VBAT on GPIO 19, /2 divider, charge status on GPIO 53) -- */
void odroid_input_battery_level_init(void)
{
    if (s_bat_ok) return;
    if (!adc_pin_setup(PIN_ANA_VBAT, &s_bat_unit, &s_bat_chan)) return;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = s_bat_unit,
        .chan     = s_bat_chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_bat_cali) != ESP_OK)
        s_bat_cali = NULL;
#endif
    s_bat_ok = true;
    ESP_LOGI(TAG, "Battery ADC ready on GPIO %d (%s)", PIN_ANA_VBAT,
             s_bat_cali ? "calibrated" : "nominal scale");
}

void odroid_input_battery_level_read(odroid_battery_state *state)
{
    if (!state) return;

    state->charging = (gpio_get_level((gpio_num_t)PIN_CHG_STAT) == 0);

    if (!s_bat_ok) {
        state->millivolts = 4200;
        state->percentage = 100;
        return;
    }

    int sum = 0, n = 0, raw = 0;
    adc_oneshot_read(s_adc_unit[s_bat_unit], s_bat_chan, &raw);   /* discard first conversion */
    for (int i = 0; i < 8; i++) {
        if (adc_oneshot_read(s_adc_unit[s_bat_unit], s_bat_chan, &raw) == ESP_OK) { sum += raw; n++; }
    }
    int raw_avg = n ? sum / n : 0;

    int pin_mv = 0;
    if (!(s_bat_cali && adc_cali_raw_to_voltage(s_bat_cali, raw_avg, &pin_mv) == ESP_OK))
        pin_mv = raw_avg * 4400 / 4095;           /* nominal 12 dB full scale */
    int bat_mv = pin_mv * 2;                      /* /2 divider */

    int pct = (bat_mv - PWR_BAT_MIN_VOL) * 100 / (PWR_BAT_MAX_VOL - PWR_BAT_MIN_VOL);
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;

    state->millivolts = bat_mv;
    state->percentage = pct;
}

void odroid_input_battery_monitor_enabled_set(bool enabled)
{
    (void)enabled;
}

bool odroid_input_gpio_pad_detected(void)
{
    return true;   /* built-in buttons are always present */
}

bool odroid_input_usb_gamepad_connected(void)
{
    return gamepad_is_connected();
}

/* --- USB Gamepad Button Mapping - file I/O -------------------- */

#define GMAP_MAGIC 0x50414D47  /* "GMAP" little-endian */

static void usb_map_set_defaults(void)
{
    s_usb_map.btn[0] = GAMEPAD_BTN_A;
    s_usb_map.btn[1] = GAMEPAD_BTN_B;
    s_usb_map.btn[2] = GAMEPAD_BTN_X;
    s_usb_map.btn[3] = GAMEPAD_BTN_Y;
    s_usb_map.btn[4] = GAMEPAD_BTN_L1 | GAMEPAD_BTN_L2;
    s_usb_map.btn[5] = GAMEPAD_BTN_R1 | GAMEPAD_BTN_R2;
    s_usb_map.btn[6] = GAMEPAD_BTN_SELECT;
    s_usb_map.btn[7] = GAMEPAD_BTN_START;
    s_usb_map.btn[8]  = 0;
    s_usb_map.btn[9]  = 0;
    s_usb_map.btn[10] = 0;
    s_usb_map.btn[11] = 0;
}

static bool usb_map_get_path(char *buf, size_t buflen)
{
    uint16_t vid = 0, pid = 0;
    gamepad_get_vid_pid(&vid, &pid);
    if (vid == 0 && pid == 0) return false;
    snprintf(buf, buflen, "/sd/odroid/gamepad/%04X_%04X.map", vid, pid);
    return true;
}

bool odroid_input_usb_map_exists(void)
{
    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return false;
    struct stat st;
    return (stat(path, &st) == 0);
}

void odroid_input_usb_map_load(void)
{
    uint16_t vid = 0, pid = 0;
    gamepad_get_vid_pid(&vid, &pid);

    usb_map_set_defaults();
    s_usb_map_loaded = true;
    s_usb_map_vid = vid;
    s_usb_map_pid = pid;

    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGI(TAG, "No custom map for %04X:%04X, using defaults", vid, pid);
        return;
    }

    uint32_t magic = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != GMAP_MAGIC) {
        ESP_LOGW(TAG, "Invalid map file magic for %04X:%04X", vid, pid);
        fclose(f);
        return;
    }

    uint32_t data[ODROID_USB_MAP_COUNT];
    memset(data, 0, sizeof(data));
    size_t nread = fread(data, sizeof(uint32_t), ODROID_USB_MAP_COUNT, f);
    fclose(f);

    if (nread < 8) {
        ESP_LOGW(TAG, "Short map file for %04X:%04X (got %d entries)", vid, pid, (int)nread);
        usb_map_set_defaults();
        return;
    }

    for (int i = 0; i < ODROID_USB_MAP_COUNT; i++)
        s_usb_map.btn[i] = data[i];

    ESP_LOGI(TAG, "Loaded custom map for %04X:%04X", vid, pid);
}

bool odroid_input_usb_map_save(const odroid_usb_map_t *map)
{
    if (!map) return false;

    char path[80];
    if (!usb_map_get_path(path, sizeof(path))) return false;

    mkdir("/sd/odroid", 0775);
    mkdir("/sd/odroid/gamepad", 0775);

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create map file: %s", path);
        return false;
    }

    uint32_t magic = GMAP_MAGIC;
    fwrite(&magic, 4, 1, f);
    fwrite(map->btn, sizeof(uint32_t), ODROID_USB_MAP_COUNT, f);
    fclose(f);

    memcpy(&s_usb_map, map, sizeof(odroid_usb_map_t));
    ESP_LOGI(TAG, "Saved map to %s", path);
    return true;
}

void odroid_input_usb_map_get(odroid_usb_map_t *map)
{
    if (map) memcpy(map, &s_usb_map, sizeof(odroid_usb_map_t));
}

void odroid_input_usb_map_set(const odroid_usb_map_t *map)
{
    if (map) memcpy(&s_usb_map, map, sizeof(odroid_usb_map_t));
}
