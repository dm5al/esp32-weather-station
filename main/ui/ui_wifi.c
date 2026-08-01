#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.wifi";

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_status;
static lv_obj_t *s_rescan_label;
static lv_obj_t *s_back_btn;
static lv_obj_t *s_back_label;
static lv_obj_t *s_title;

/* Password prompt, built lazily and destroyed on close. */
static lv_obj_t *s_modal;
static lv_obj_t *s_pass_ta;
static char s_modal_ssid[WIFI_MGR_SSID_MAX + 1];

/* The list buttons index into this, so it has to outlive the scan callback. */
static wifi_mgr_ap_t s_aps[WIFI_MGR_MAX_APS];
static size_t s_ap_count;

static void close_modal(void)
{
    if (s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
        s_pass_ta = NULL;
    }
}

static void send_connect(const char *ssid, const char *pass)
{
    ui_cmd_t cmd = {.type = UI_CMD_WIFI_CONNECT};
    strlcpy(cmd.ssid, ssid, sizeof(cmd.ssid));
    strlcpy(cmd.pass, pass ? pass : "", sizeof(cmd.pass));
    ui_post_cmd(&cmd);

    char msg[96];
    snprintf(msg, sizeof(msg), T(STR_CONNECTING_TO), ssid);
    ui_wifi_set_status(msg, false);
}

static void on_connect_clicked(lv_event_t *e)
{
    (void)e;
    if (!s_pass_ta) {
        return;
    }
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char pass[WIFI_MGR_PASS_MAX + 1];
    strlcpy(ssid, s_modal_ssid, sizeof(ssid));
    strlcpy(pass, lv_textarea_get_text(s_pass_ta), sizeof(pass));

    /* Tear the dialog down before posting: the textarea dies with it. */
    close_modal();
    send_connect(ssid, pass);
}

static void on_cancel_clicked(lv_event_t *e)
{
    (void)e;
    close_modal();
}

/* Enter on the on-screen keyboard is the same as pressing Connect. */
static void on_keyboard_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        on_connect_clicked(e);
    }
}

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

static void open_password_modal(const char *ssid)
{
    close_modal();
    strlcpy(s_modal_ssid, ssid, sizeof(s_modal_ssid));

    /* Full-screen scrim so nothing behind can be tapped by accident. */
    s_modal = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_70, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(s_modal);
    ui_style_card(card);
    lv_obj_set_size(card, 700, 200);
    lv_obj_set_pos(card, (800 - 700) / 2, 16);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &lv_font_ui_20, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_label_set_text_fmt(title, T(STR_PASSWORD_FOR), ssid);
    lv_obj_set_pos(title, 24, 20);

    s_pass_ta = lv_textarea_create(card);
    lv_obj_set_size(s_pass_ta, 652, 56);
    lv_obj_set_pos(s_pass_ta, 24, 56);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, T(STR_PASSWORD_HINT));
    lv_textarea_set_max_length(s_pass_ta, WIFI_MGR_PASS_MAX);
    lv_obj_set_style_text_font(s_pass_ta, &lv_font_ui_20, 0);
    lv_obj_set_style_bg_color(s_pass_ta, UI_COL_BG, 0);
    lv_obj_set_style_text_color(s_pass_ta, UI_COL_TEXT, 0);
    lv_obj_set_style_border_color(s_pass_ta, UI_COL_CARD_HI, 0);
    lv_obj_set_style_border_width(s_pass_ta, 2, 0);

    lv_obj_t *cancel = make_button(card, NULL, T(STR_CANCEL), UI_COL_CARD_HI, 150, 48);
    lv_obj_set_pos(cancel, 372, 130);
    lv_obj_add_event_cb(cancel, on_cancel_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_label = NULL;
    lv_obj_t *connect = make_button(card, &connect_label, T(STR_CONNECT), UI_COL_ACCENT, 150, 48);
    lv_obj_set_pos(connect, 526, 130);
    lv_obj_set_style_text_color(connect_label, UI_COL_BG, 0);
    lv_obj_add_event_cb(connect, on_connect_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(s_modal);
    lv_obj_set_size(kb, 800, 244);
    /* lv_keyboard's constructor aligns itself BOTTOM_MID, so lv_obj_set_pos()
     * here would be an offset from the bottom edge and push it off-screen.
     * Align explicitly instead. */
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, s_pass_ta);
    lv_obj_add_event_cb(kb, on_keyboard_event, LV_EVENT_READY, NULL);

    /* Match the rest of the UI — the stock theme is light and glares here. */
    lv_obj_set_style_bg_color(kb, UI_COL_BG, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 6, 0);
    lv_obj_set_style_text_font(kb, &lv_font_ui_20, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, UI_COL_CARD, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, UI_COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, UI_COL_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
}

static void on_ap_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(btn);
    if (idx >= s_ap_count) {
        return;
    }
    if (s_aps[idx].secure) {
        open_password_modal(s_aps[idx].ssid);
    } else {
        send_connect(s_aps[idx].ssid, ""); /* open network — nothing to type */
    }
}

static void on_rescan_clicked(lv_event_t *e)
{
    (void)e;
    ui_wifi_set_status(T(STR_SCANNING), false);
    const ui_cmd_t cmd = {.type = UI_CMD_WIFI_SCAN};
    ui_post_cmd(&cmd);
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_show_weather();
}

lv_obj_t *ui_wifi_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, UI_COL_BG, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_back_btn = make_button(s_scr, &s_back_label, "", UI_COL_CARD_HI, 150, 48);
    lv_obj_set_pos(s_back_btn, 40, 20);
    lv_obj_add_event_cb(s_back_btn, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN); /* revealed once weather exists */

    s_title = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_title, &lv_font_ui_28, 0);
    lv_obj_set_style_text_color(s_title, UI_COL_TEXT, 0);
    lv_obj_set_pos(s_title, 40, 26);

    lv_obj_t *rescan = make_button(s_scr, &s_rescan_label, "", UI_COL_CARD_HI, 190, 48);
    lv_obj_set_pos(rescan, 570, 20);
    lv_obj_add_event_cb(rescan, on_rescan_clicked, LV_EVENT_CLICKED, NULL);

    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 720, 330);
    lv_obj_set_pos(s_list, 40, 84);
    ui_style_card(s_list);
    lv_obj_set_style_pad_all(s_list, 8, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &lv_font_ui_18, 0);
    lv_obj_set_style_text_color(s_status, UI_COL_MUTED, 0);
    lv_label_set_text(s_status, "");
    lv_obj_set_pos(s_status, 40, 430);

    ui_wifi_retranslate();
    return s_scr;
}

void ui_wifi_set_aps(const wifi_mgr_ap_t *aps, size_t count)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);

    if (count > WIFI_MGR_MAX_APS) {
        count = WIFI_MGR_MAX_APS;
    }
    memcpy(s_aps, aps, count * sizeof(wifi_mgr_ap_t));
    s_ap_count = count;

    for (size_t i = 0; i < count; i++) {
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_WIFI, s_aps[i].ssid);
        lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
        lv_obj_set_style_bg_color(btn, UI_COL_CARD, 0);
        lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
        lv_obj_set_style_text_font(btn, &lv_font_ui_20, 0);
        lv_obj_set_style_bg_color(btn, UI_COL_CARD_HI, LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, on_ap_clicked, LV_EVENT_CLICKED, NULL);

        /* Right-aligned signal strength; the keyboard glyph flags "needs a
         * password" — LVGL's symbol font has no padlock. */
        lv_obj_t *meta = lv_label_create(btn);
        lv_obj_set_style_text_font(meta, &lv_font_ui_16, 0);
        lv_obj_set_style_text_color(meta, UI_COL_MUTED, 0);
        lv_label_set_text_fmt(meta, "%s%d dBm", s_aps[i].secure ? LV_SYMBOL_KEYBOARD "  " : "",
                              s_aps[i].rssi);
        lv_obj_align(meta, LV_ALIGN_RIGHT_MID, -12, 0);
    }

    ui_wifi_set_status(count == 0 ? T(STR_NO_NETWORKS) : T(STR_TAP_NETWORK), count == 0);
    ESP_LOGI(TAG, "listed %u networks", (unsigned)count);
}

void ui_wifi_set_status(const char *text, bool error)
{
    if (!s_status) {
        return;
    }
    lv_label_set_text(s_status, text ? text : "");
    lv_obj_set_style_text_color(s_status, error ? UI_COL_DANGER : UI_COL_MUTED, 0);
}

void ui_wifi_show_back(bool visible)
{
    if (!s_back_btn) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_title, 210, 26); /* make room for the button */
    } else {
        lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_title, 40, 26);
    }
}

void ui_wifi_retranslate(void)
{
    if (!s_scr) {
        return;
    }
    lv_label_set_text(s_title, T(STR_CHOOSE_NETWORK));
    lv_label_set_text_fmt(s_back_label, LV_SYMBOL_LEFT "  %s", T(STR_BACK));
    lv_label_set_text_fmt(s_rescan_label, LV_SYMBOL_REFRESH "  %s", T(STR_RESCAN));
    /* The status line is transient; restore the neutral prompt rather than
     * leaving a stale message in the previous language. */
    ui_wifi_set_status(s_ap_count ? T(STR_TAP_NETWORK) : "", false);
}
