/*
 * Where the forecast is for.
 *
 * Two modes, chosen with a pair of buttons: automatic, which asks the internet
 * connection, and manual, which is a place the operator picked by name.
 *
 * Automatic is right for most people and needs no setup, but it is only ever as
 * good as the IP address. Behind a VPN, on mobile broadband, or on a line whose
 * registered address is at the far end of the country, it can be a long way out
 * - and a weather display that is confidently wrong about where it is is worse
 * than one that asks.
 *
 * The search runs on the app task, not here: LVGL callbacks must never block on
 * the network, so the query is posted as a command and the results arrive later
 * through ui_location_set_results().
 */
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "esp_log.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.loc";

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_back_label;

static lv_obj_t *s_mode_btn[2];
static lv_obj_t *s_mode_label[2];
static lv_obj_t *s_mode_desc;
static lv_obj_t *s_current;
static lv_obj_t *s_note;

static lv_obj_t *s_search_ta;
static lv_obj_t *s_search_label;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_kb;

/* The list buttons index into this, so it outlives the search callback. */
static geo_location_t s_results[GEO_SEARCH_MAX];
static size_t s_result_count;

/* ---- helpers ------------------------------------------------------------- */

static lv_obj_t *make_button(lv_obj_t *parent, lv_obj_t **out_label, const char *text,
                             lv_color_t bg, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_ui_18, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
    lv_obj_center(l);
    if (out_label) {
        *out_label = l;
    }
    return btn;
}

/** @brief "Bad Marienberg, Rhineland-Palatinate, Germany", skipping the blanks. */
static void describe(const geo_location_t *g, char *buf, size_t buf_sz)
{
    if (!g->city[0] && !g->country[0]) {
        snprintf(buf, buf_sz, "%s", T(STR_NOT_SET));
        return;
    }
    if (g->region[0] && g->country[0]) {
        snprintf(buf, buf_sz, "%s, %s, %s", g->city, g->region, g->country);
    } else if (g->country[0]) {
        snprintf(buf, buf_sz, "%s, %s", g->city, g->country);
    } else {
        snprintf(buf, buf_sz, "%s", g->city);
    }
}

/* ---- painting ------------------------------------------------------------ */

static void paint_mode(void)
{
    const geo_mode_t mode = geo_get_mode();

    for (int i = 0; i < 2; i++) {
        const bool active = (i == (int)mode);
        lv_obj_set_style_bg_color(s_mode_btn[i], active ? UI_COL_ACCENT : UI_COL_CARD_HI, 0);
        lv_obj_set_style_text_color(s_mode_label[i], active ? UI_COL_BG : UI_COL_TEXT, 0);
    }

    lv_label_set_text(s_mode_desc,
                      mode == GEO_MODE_MANUAL ? T(STR_LOC_MANUAL_DESC) : T(STR_LOC_AUTO_DESC));

    /*
     * The search is only shown in manual mode. Leaving it visible but inert in
     * automatic would invite the operator to type a place and then wonder why
     * the forecast did not move.
     */
    const bool manual = (mode == GEO_MODE_MANUAL);
    lv_obj_t *const manual_only[] = {s_search_ta, s_search_label, s_list, s_note};
    for (size_t i = 0; i < sizeof(manual_only) / sizeof(manual_only[0]); i++) {
        if (manual) {
            lv_obj_clear_flag(manual_only[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(manual_only[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    char buf[160];
    geo_location_t place = {0};
    if (mode == GEO_MODE_MANUAL && geo_get_manual(&place) == ESP_OK) {
        describe(&place, buf, sizeof(buf));
    } else if (mode == GEO_MODE_MANUAL) {
        snprintf(buf, sizeof(buf), "%s", T(STR_NOT_SET));
    } else if (geo_load_cached(&place) == ESP_OK) {
        describe(&place, buf, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%s", T(STR_NOT_SET));
    }
    lv_label_set_text_fmt(s_current, LV_SYMBOL_GPS "  %s", buf);
}

void ui_location_refresh(void)
{
    if (s_scr) {
        paint_mode();
    }
}

void ui_location_set_status(const char *text, bool error)
{
    if (!s_status) {
        return;
    }
    lv_label_set_text(s_status, text ? text : "");
    lv_obj_set_style_text_color(s_status, error ? UI_COL_WARM : UI_COL_MUTED, 0);
}

/* ---- events -------------------------------------------------------------- */

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static void on_mode_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const geo_mode_t mode = (geo_mode_t)(uintptr_t)lv_obj_get_user_data(btn);

    if (mode == GEO_MODE_AUTO) {
        /* The app task owns the switch, because it also has to throw away the
         * location it is holding and fetch again. */
        const ui_cmd_t cmd = {.type = UI_CMD_LOCATION_AUTO};
        ui_post_cmd(&cmd);
    } else {
        geo_set_mode(GEO_MODE_MANUAL);
    }
    paint_mode();
}

static void send_search(void)
{
    if (!s_search_ta) {
        return;
    }
    const char *q = lv_textarea_get_text(s_search_ta);
    if (!q || !q[0]) {
        return;
    }
    ui_cmd_t cmd = {.type = UI_CMD_SEARCH_PLACE};
    strlcpy(cmd.text, q, sizeof(cmd.text));
    ui_post_cmd(&cmd);

    lv_obj_clean(s_list);
    s_result_count = 0;
    ui_location_set_status(T(STR_SEARCHING), false);
}

static void on_search_clicked(lv_event_t *e)
{
    (void)e;
    send_search();
}

/* The keyboard's tick button, which is where a search naturally ends. */
static void on_keyboard_ready(lv_event_t *e)
{
    (void)e;
    send_search();
    if (s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_search_focus(lv_event_t *e)
{
    (void)e;
    if (s_kb) {
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_search_defocus(lv_event_t *e)
{
    (void)e;
    if (s_kb) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_result_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(btn);
    if (idx >= s_result_count) {
        return;
    }

    ui_cmd_t cmd = {.type = UI_CMD_SET_LOCATION};
    cmd.place = s_results[idx];
    ui_post_cmd(&cmd);

    char buf[160];
    describe(&s_results[idx], buf, sizeof(buf));
    ESP_LOGI(TAG, "chose %s", buf);

    paint_mode();
}

/* ---- results ------------------------------------------------------------- */

void ui_location_set_results(const geo_location_t *places, size_t count)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);

    if (count > GEO_SEARCH_MAX) {
        count = GEO_SEARCH_MAX;
    }
    if (places && count) {
        memcpy(s_results, places, count * sizeof(geo_location_t));
    }
    s_result_count = count;

    for (size_t i = 0; i < count; i++) {
        char buf[160];
        describe(&s_results[i], buf, sizeof(buf));

        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_GPS, buf);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
        lv_obj_set_style_bg_color(btn, UI_COL_CARD, 0);
        lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(btn, &lv_font_ui_18, 0);
        lv_obj_set_style_bg_color(btn, UI_COL_CARD_HI, LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, on_result_clicked, LV_EVENT_CLICKED, NULL);
    }

    ui_location_set_status(count == 0 ? T(STR_NO_RESULTS) : "", count == 0);
    ESP_LOGI(TAG, "listed %u places", (unsigned)count);
}

/* ---- construction -------------------------------------------------------- */

lv_obj_t *ui_location_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, UI_COL_BG, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = make_button(s_scr, &s_back_label, "", UI_COL_CARD_HI, 150, 48);
    lv_obj_set_pos(back, 40, 20);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &lv_font_ui_28, 0);
    lv_obj_set_style_text_color(s_title, UI_COL_TEXT, 0);
    lv_obj_set_pos(s_title, 210, 26);

    /* ---- mode ---- */
    lv_obj_t *card = lv_obj_create(s_scr);
    ui_style_card(card);
    lv_obj_set_size(card, BSP_LCD_H_RES - 80, 128);
    lv_obj_set_pos(card, 40, 84);

    static const str_id_t k_mode_text[2] = {STR_LOC_AUTO, STR_LOC_MANUAL};
    for (int i = 0; i < 2; i++) {
        s_mode_btn[i] = make_button(card, &s_mode_label[i], T(k_mode_text[i]), UI_COL_CARD_HI,
                                    210, 52);
        lv_obj_set_pos(s_mode_btn[i], 24 + i * 230, 16);
        lv_obj_set_user_data(s_mode_btn[i], (void *)(uintptr_t)i);
        lv_obj_set_style_text_font(s_mode_label[i], &lv_font_ui_20, 0);
        lv_obj_add_event_cb(s_mode_btn[i], on_mode_clicked, LV_EVENT_CLICKED, NULL);
    }

    s_mode_desc = lv_label_create(card);
    lv_obj_set_style_text_font(s_mode_desc, &lv_font_ui_14, 0);
    lv_obj_set_style_text_color(s_mode_desc, UI_COL_MUTED, 0);
    lv_obj_set_pos(s_mode_desc, 496, 24);

    s_current = lv_label_create(card);
    lv_obj_set_style_text_font(s_current, &lv_font_ui_16, 0);
    lv_obj_set_style_text_color(s_current, UI_COL_TEXT, 0);
    lv_obj_set_pos(s_current, 24, 84);

    /* ---- search ---- */
    s_search_ta = lv_textarea_create(s_scr);
    lv_obj_set_size(s_search_ta, BSP_LCD_H_RES - 80 - 170, 52);
    lv_obj_set_pos(s_search_ta, 40, 226);
    lv_textarea_set_one_line(s_search_ta, true);
    lv_textarea_set_max_length(s_search_ta, 48);
    lv_obj_set_style_text_font(s_search_ta, &lv_font_ui_20, 0);
    lv_obj_set_style_bg_color(s_search_ta, UI_COL_CARD, 0);
    lv_obj_set_style_text_color(s_search_ta, UI_COL_TEXT, 0);
    lv_obj_set_style_border_color(s_search_ta, UI_COL_CARD_HI, 0);
    lv_obj_set_style_border_width(s_search_ta, 2, 0);

    lv_obj_t *search = make_button(s_scr, &s_search_label, "", UI_COL_ACCENT, 150, 52);
    lv_obj_set_pos(search, BSP_LCD_H_RES - 40 - 150, 226);
    lv_obj_set_style_text_color(s_search_label, UI_COL_BG, 0);
    lv_obj_add_event_cb(search, on_search_clicked, LV_EVENT_CLICKED, NULL);

    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &lv_font_ui_14, 0);
    lv_obj_set_style_text_color(s_status, UI_COL_MUTED, 0);
    lv_obj_set_pos(s_status, 40, 286);

    s_note = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_note, &lv_font_ui_12, 0);
    lv_obj_set_style_text_color(s_note, UI_COL_MUTED, 0);
    lv_obj_set_pos(s_note, 40, BSP_LCD_V_RES - 26);

    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, BSP_LCD_H_RES - 80, BSP_LCD_V_RES - 310 - 34);
    lv_obj_set_pos(s_list, 40, 308);
    ui_style_card(s_list);
    lv_obj_set_style_pad_all(s_list, 6, 0);

    /*
     * The keyboard lives on the screen rather than in a modal: choosing a place
     * is the whole purpose of being here, so there is nothing to hide behind it
     * and nothing else the operator might want to reach first.
     */
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, BSP_LCD_H_RES, 244);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_kb, s_search_ta);
    lv_obj_add_event_cb(s_kb, on_keyboard_ready, LV_EVENT_READY, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_color(s_kb, UI_COL_BG, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_pad_all(s_kb, 6, 0);
    lv_obj_set_style_text_font(s_kb, &lv_font_ui_20, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, UI_COL_CARD, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, UI_COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, UI_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_kb, 8, LV_PART_ITEMS);

    /* Raised only while the field has focus, so the results are readable the
     * rest of the time. */
    lv_obj_add_event_cb(s_search_ta, on_search_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_search_ta, on_search_defocus, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(s_search_ta, on_search_focus, LV_EVENT_CLICKED, NULL);

    ui_location_retranslate();
    return s_scr;
}

void ui_location_retranslate(void)
{
    if (!s_scr) {
        return;
    }
    lv_label_set_text(s_title, T(STR_LOCATION));
    lv_label_set_text_fmt(s_back_label, LV_SYMBOL_LEFT "  %s", T(STR_BACK));
    lv_label_set_text(s_mode_label[GEO_MODE_AUTO], T(STR_LOC_AUTO));
    lv_label_set_text(s_mode_label[GEO_MODE_MANUAL], T(STR_LOC_MANUAL));
    lv_label_set_text(s_search_label, T(STR_SEARCH));
    lv_label_set_text(s_note, T(STR_LOC_HOLIDAY_NOTE));
    lv_textarea_set_placeholder_text(s_search_ta, T(STR_SEARCH_CITY));
    paint_mode();
}
