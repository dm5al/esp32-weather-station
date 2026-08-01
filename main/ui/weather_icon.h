#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "net/weather.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a weather glyph out of plain LVGL objects.
 *
 * The icons are composed from gradient-filled discs and rounded bars rather
 * than a bitmap font, so they scale to any size and cost no flash.
 *
 * Each icon sits on a rounded "sky" tile tinted to match the conditions, so it
 * carries its own colour instead of relying on whatever is behind it.
 *
 * @param parent  Parent object
 * @param kind    Icon family from weather_code_icon()
 * @param size    Width and height of the square the icon is drawn in
 * @param animate Run the idle animations (drifting cloud, falling rain, ...).
 *                Reserve this for the one large icon: every animation
 *                invalidates its area continuously, and with the frame buffers
 *                in PSRAM that redraw traffic competes with the LCD DMA.
 * @return The icon container. Deleting it also stops its animations.
 */
lv_obj_t *weather_icon_create(lv_obj_t *parent, weather_icon_t kind, int size, bool animate);

#ifdef __cplusplus
}
#endif
