#include "ui/ui.h"

#include "esp_log.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui";

static lv_obj_t *s_scr_status;
static lv_obj_t *s_scr_wifi;
static lv_obj_t *s_scr_weather;
static lv_obj_t *s_scr_settings;

static lv_obj_t *s_status_title;
static lv_obj_t *s_status_detail;
static lv_obj_t *s_status_spinner;

void ui_style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, UI_COL_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_status_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 18, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    s_status_spinner = lv_spinner_create(col);
    lv_obj_set_size(s_status_spinner, 68, 68);
    lv_obj_set_style_arc_color(s_status_spinner, UI_COL_CARD_HI, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_status_spinner, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_status_spinner, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_status_spinner, 7, LV_PART_INDICATOR);

    s_status_title = lv_label_create(col);
    lv_obj_set_style_text_font(s_status_title, &lv_font_ui_28, 0);
    lv_obj_set_style_text_color(s_status_title, UI_COL_TEXT, 0);
    lv_label_set_text(s_status_title, "");

    s_status_detail = lv_label_create(col);
    lv_obj_set_style_text_font(s_status_detail, &lv_font_ui_18, 0);
    lv_obj_set_style_text_color(s_status_detail, UI_COL_MUTED, 0);
    lv_obj_set_style_text_align(s_status_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_detail, LV_PCT(100));
    lv_label_set_text(s_status_detail, "");

    return scr;
}

void ui_init(void)
{
    s_scr_status = create_status_screen();
    s_scr_wifi = ui_wifi_create();
    s_scr_weather = ui_weather_create();
    s_scr_settings = ui_settings_create();

    lv_screen_load(s_scr_status);
    ESP_LOGI(TAG, "screens created");
}

void ui_show_status(const char *title, const char *detail, bool busy)
{
    if (!s_scr_status) {
        return;
    }
    lv_label_set_text(s_status_title, title ? title : "");
    lv_label_set_text(s_status_detail, detail ? detail : "");
    if (busy) {
        lv_obj_clear_flag(s_status_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_status_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_screen_active() != s_scr_status) {
        lv_screen_load(s_scr_status);
    }
}

void ui_show_settings(void)
{
    if (s_scr_settings) {
        /* Refresh the network line: it can have changed since the last visit. */
        ui_settings_retranslate();
        if (lv_screen_active() != s_scr_settings) {
            lv_screen_load(s_scr_settings);
        }
    }
}

void ui_retranslate(void)
{
    ui_wifi_retranslate();
    ui_weather_retranslate();
    ui_settings_retranslate();
}

void ui_show_wifi(void)
{
    if (!s_scr_wifi) {
        return;
    }
    /* Only offer Back once there is a weather screen worth returning to. */
    ui_wifi_show_back(ui_weather_has_data());
    if (lv_screen_active() != s_scr_wifi) {
        lv_screen_load(s_scr_wifi);
    }
}

void ui_show_weather(void)
{
    if (s_scr_weather && lv_screen_active() != s_scr_weather) {
        lv_screen_load(s_scr_weather);
    }
}

/* ---- Panel alignment overlay -------------------------------------------- */

static lv_obj_t *s_grid;

/** @brief Undecorated filled rectangle on the grid overlay. */
static void grid_bar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

void ui_set_alignment_grid(bool on)
{
    if (!on) {
        if (s_grid) {
            lv_obj_delete(s_grid);
            s_grid = NULL;
        }
        return;
    }
    if (s_grid) {
        return;
    }

    const int w = LV_HOR_RES;
    const int h = LV_VER_RES;

    s_grid = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, w, h);
    lv_obj_set_pos(s_grid, 0, 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    const lv_color_t frame = lv_color_hex(0xFF3B30);
    const lv_color_t tick = lv_color_hex(0x34C759);
    const lv_color_t cross = lv_color_hex(0x0A84FF);

    /* 1px frame on the outermost pixels — the reference for "is it clipped?". */
    grid_bar(s_grid, 0, 0, w, 1, frame);
    grid_bar(s_grid, 0, h - 1, w, 1, frame);
    grid_bar(s_grid, 0, 0, 1, h, frame);
    grid_bar(s_grid, w - 1, 0, 1, h, frame);

    /* Corner brackets: thick enough to spot at a glance from across the desk. */
    const int arm = 40;
    const int thick = 4;
    grid_bar(s_grid, 0, 0, arm, thick, frame);
    grid_bar(s_grid, 0, 0, thick, arm, frame);
    grid_bar(s_grid, w - arm, 0, arm, thick, frame);
    grid_bar(s_grid, w - thick, 0, thick, arm, frame);
    grid_bar(s_grid, 0, h - thick, arm, thick, frame);
    grid_bar(s_grid, 0, h - arm, thick, arm, frame);
    grid_bar(s_grid, w - arm, h - thick, arm, thick, frame);
    grid_bar(s_grid, w - thick, h - arm, thick, arm, frame);

    /* Ticks every 50px, taller every 100px, for counting the offset. */
    for (int x = 50; x < w; x += 50) {
        int len = (x % 100 == 0) ? 20 : 10;
        grid_bar(s_grid, x, 0, 1, len, tick);
        grid_bar(s_grid, x, h - len, 1, len, tick);
    }
    for (int y = 50; y < h; y += 50) {
        int len = (y % 100 == 0) ? 20 : 10;
        grid_bar(s_grid, 0, y, len, 1, tick);
        grid_bar(s_grid, w - len, y, len, 1, tick);
    }

    grid_bar(s_grid, w / 2, h / 2 - 30, 1, 60, cross);
    grid_bar(s_grid, w / 2 - 30, h / 2, 60, 1, cross);

    lv_obj_t *hint = lv_label_create(s_grid);
    lv_label_set_text(hint, "alignment grid — all four red edges must be visible");
    lv_obj_set_style_text_font(hint, &lv_font_ui_16, 0);
    lv_obj_set_style_text_color(hint, frame, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);
}
