/*
 * UI typeface.
 *
 * These replace LVGL's built-in Montserrat faces, which carry only ASCII plus
 * the symbol block and therefore cannot render Russian or German. Same
 * typeface (Montserrat-Medium), rebuilt with Latin-1 and Cyrillic added.
 *
 * Regenerate with tools/gen_fonts.sh.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(lv_font_ui_12)
LV_FONT_DECLARE(lv_font_ui_14)
LV_FONT_DECLARE(lv_font_ui_16)
LV_FONT_DECLARE(lv_font_ui_18)
LV_FONT_DECLARE(lv_font_ui_20)
LV_FONT_DECLARE(lv_font_ui_24)
LV_FONT_DECLARE(lv_font_ui_28)

/* Numerals, sign, separators and the degree sign only — this face renders
 * nothing but the big temperature readout. */
LV_FONT_DECLARE(lv_font_ui_48)

#ifdef __cplusplus
}
#endif
