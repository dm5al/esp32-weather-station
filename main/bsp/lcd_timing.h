/*
 * Runtime-adjustable RGB panel timings.
 *
 * The Waveshare 7" panel's exact porch values are not documented, and the
 * figures floating around for it disagree by an order of magnitude (4 to 210).
 * Rather than burn a 2-minute build+flash cycle per guess, the timings live in
 * NVS and can be changed from the serial console (see lcd_console.h): each
 * tweak costs a reboot, about a second.
 *
 * Once the picture lines up, copy the values from `lcd show` into the
 * LCD_TIMING_DEFAULT_* macros below so a fresh board is right out of the box.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compiled-in fallbacks, used until something is saved to NVS. */
#define LCD_TIMING_DEFAULT_PCLK_MHZ 21
#define LCD_TIMING_DEFAULT_HPW      4
#define LCD_TIMING_DEFAULT_HBP      40
#define LCD_TIMING_DEFAULT_HFP      20
#define LCD_TIMING_DEFAULT_VPW      4
#define LCD_TIMING_DEFAULT_VBP      16
#define LCD_TIMING_DEFAULT_VFP      16
#define LCD_TIMING_DEFAULT_BB_LINES 10

typedef struct {
    uint32_t pclk_mhz;

    uint16_t hpw; /* HSYNC pulse width, in PCLKs */
    uint16_t hbp; /* horizontal back porch — this is the knob that moves the
                   * picture sideways: larger pushes it right, smaller left */
    uint16_t hfp; /* horizontal front porch */

    uint16_t vpw; /* VSYNC pulse width, in lines */
    uint16_t vbp; /* vertical back porch — moves the picture up/down */
    uint16_t vfp; /* vertical front porch */

    /*
     * Height in lines of each of the two internal-SRAM bounce buffers. The DMA
     * feeds the panel from these rather than straight from PSRAM, which is what
     * makes it robust against short PSRAM bandwidth spikes.
     *
     * 0 disables bounce mode and hands the panel raw PSRAM frame buffers.
     * Must divide 480 evenly: the driver's underrun detector computes
     * fb_size/bb_size and a remainder makes that count wrong.
     * Costs bb_lines * 800 * 2 * 2 bytes of internal RAM (32 KB at 10 lines).
     */
    uint16_t bb_lines;

    uint8_t hsync_idle_low;   /* HSYNC idles low instead of high */
    uint8_t vsync_idle_low;   /* VSYNC idles low instead of high */
    uint8_t de_idle_high;     /* DE idles high instead of low */
    uint8_t pclk_active_neg;  /* latch data on the falling PCLK edge */
} lcd_timing_t;

/** @brief Fill @p t with the compiled-in defaults. */
void lcd_timing_defaults(lcd_timing_t *t);

/** @brief Defaults, overlaid with whatever is stored in NVS. Never fails. */
void lcd_timing_load(lcd_timing_t *t);

/** @brief Persist @p t so it survives a reboot. */
esp_err_t lcd_timing_save(const lcd_timing_t *t);

/** @brief Drop the stored override and go back to the compiled-in defaults. */
esp_err_t lcd_timing_clear(void);

/** @brief True if the values currently in use came from NVS, not the defaults. */
bool lcd_timing_is_overridden(void);

#ifdef __cplusplus
}
#endif
