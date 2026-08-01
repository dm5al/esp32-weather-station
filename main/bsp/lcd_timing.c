#include "bsp/lcd_timing.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "lcd_timing";

#define NVS_NS  "lcd"
#define NVS_KEY "timing"

static bool s_overridden;

void lcd_timing_defaults(lcd_timing_t *t)
{
    if (!t) {
        return;
    }
    *t = (lcd_timing_t){
        .pclk_mhz = LCD_TIMING_DEFAULT_PCLK_MHZ,
        .hpw = LCD_TIMING_DEFAULT_HPW,
        .hbp = LCD_TIMING_DEFAULT_HBP,
        .hfp = LCD_TIMING_DEFAULT_HFP,
        .vpw = LCD_TIMING_DEFAULT_VPW,
        .vbp = LCD_TIMING_DEFAULT_VBP,
        .vfp = LCD_TIMING_DEFAULT_VFP,
        .bb_lines = LCD_TIMING_DEFAULT_BB_LINES,
        .hsync_idle_low = 0,
        .vsync_idle_low = 0,
        .de_idle_high = 0,
        .pclk_active_neg = 1,
    };
}

void lcd_timing_load(lcd_timing_t *t)
{
    lcd_timing_defaults(t);
    s_overridden = false;

    nvs_handle_t h;
    /* Nothing saved yet is the normal case, not an error. */
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    lcd_timing_t stored;
    size_t len = sizeof(stored);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &stored, &len);
    nvs_close(h);

    /* A size mismatch means the struct changed since it was written; fall back
     * to defaults rather than loading garbage into the LCD peripheral. */
    if (err == ESP_OK && len == sizeof(stored)) {
        *t = stored;
        s_overridden = true;
        ESP_LOGI(TAG, "using stored timings from NVS");
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored timings have stale layout, ignoring");
    }
}

esp_err_t lcd_timing_save(const lcd_timing_t *t)
{
    if (!t) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, t, sizeof(*t));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t lcd_timing_clear(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(h, NVS_KEY);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool lcd_timing_is_overridden(void)
{
    return s_overridden;
}
