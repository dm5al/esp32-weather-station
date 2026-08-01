#pragma once

#include "lvgl.h"

/* Screen constructors, implemented one per file. */
lv_obj_t *ui_wifi_create(void);
lv_obj_t *ui_weather_create(void);
lv_obj_t *ui_settings_create(void);

/* Re-render every string after a language change. Each screen owns its own
 * labels, so each has to be asked individually. */
void ui_wifi_retranslate(void);
void ui_weather_retranslate(void);
void ui_settings_retranslate(void);

/** @brief Apply the shared card look (fill, radius, no border, no scrollbar). */
void ui_style_card(lv_obj_t *obj);

/**
 * @brief Show or hide the picker's Back button.
 *
 * Hidden during first-run setup: there is no weather screen to go back to yet,
 * and a button that leads nowhere is worse than no button.
 */
void ui_wifi_show_back(bool visible);
