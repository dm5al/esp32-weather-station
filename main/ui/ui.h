#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "net/geolocate.h"
#include "net/weather.h"
#include "net/wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Palette -------------------------------------------------------------
 * A dark slate theme: the panel is bright and this runs all day indoors.
 */
#define UI_COL_BG      lv_color_hex(0x0F172A)
#define UI_COL_CARD    lv_color_hex(0x1E293B)
#define UI_COL_CARD_HI lv_color_hex(0x334155)
#define UI_COL_TEXT    lv_color_hex(0xF1F5F9)
#define UI_COL_MUTED   lv_color_hex(0x94A3B8)
#define UI_COL_ACCENT  lv_color_hex(0x38BDF8)
#define UI_COL_WARM    lv_color_hex(0xFBBF24)
#define UI_COL_COOL    lv_color_hex(0x60A5FA)
#define UI_COL_DANGER  lv_color_hex(0xF87171)
/* Sundays and public holidays, the way a wall calendar prints them. */
#define UI_COL_DAYOFF  lv_color_hex(0xFF6B6B)

/**
 * @brief Commands the UI hands back to the application task.
 *
 * LVGL callbacks run on the LVGL task, which must never block on the network.
 * Every button press therefore turns into one of these and is posted to the
 * app task, which owns all the slow work.
 */
typedef enum {
    UI_CMD_WIFI_SCAN,
    UI_CMD_WIFI_CONNECT,
    UI_CMD_REFRESH,
} ui_cmd_type_t;

typedef struct {
    ui_cmd_type_t type;
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char pass[WIFI_MGR_PASS_MAX + 1];
} ui_cmd_t;

/** @brief Implemented by main.c; must be safe to call from the LVGL task. */
void ui_post_cmd(const ui_cmd_t *cmd);

/** @brief Build the screens. Call with the LVGL lock held. */
void ui_init(void);

/** @brief Show the boot/status screen with a message. */
void ui_show_status(const char *title, const char *detail, bool busy);

/** @brief Show the network picker. */
void ui_show_wifi(void);

/** @brief Show the weather screen. */
void ui_show_weather(void);

/** @brief Show the settings screen (language, network). */
void ui_show_settings(void);

/** @brief Re-render every screen's text after a language change. */
void ui_retranslate(void);

/** @brief Replace the AP list in the picker (call after a scan completes). */
void ui_wifi_set_aps(const wifi_mgr_ap_t *aps, size_t count);

/** @brief Show progress/failure text on the picker. */
void ui_wifi_set_status(const char *text, bool error);

/** @brief Push new readings into the weather screen. */
void ui_weather_update(const geo_location_t *loc, const weather_data_t *data);

/**
 * @brief True once real readings have been shown.
 *
 * Used to decide whether an error is worth taking over the screen for: if there
 * is a last-known forecast up, stale data beats an error page.
 */
bool ui_weather_has_data(void);

/** @brief Update the link indicator in the weather screen's header. */
void ui_weather_set_link(wifi_mgr_state_t state, int8_t rssi);

/** @brief Mark the weather screen as refreshing (spins the refresh icon). */
void ui_weather_set_busy(bool busy);

/**
 * @brief Overlay a panel-alignment pattern on the top layer.
 *
 * Draws a 1px frame hugging all four edges, corner brackets, a centre cross and
 * 50px tick marks. If the picture is offset, the frame is clipped on one side
 * and gapped on the other, and the ticks let you count how many pixels out it
 * is. Sits on lv_layer_top(), so it survives screen changes.
 */
void ui_set_alignment_grid(bool on);

#ifdef __cplusplus
}
#endif
