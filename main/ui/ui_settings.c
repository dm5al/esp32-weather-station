#include <string.h>

#include "esp_log.h"
#include "net/wifi_manager.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "ui/ui_priv.h"

static const char *TAG = "ui.settings";

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_back_label;
static lv_obj_t *s_lang_heading;
static lv_obj_t *s_lang_desc;
static lv_obj_t *s_lang_label[LANG_COUNT];
static lv_obj_t *s_lang_btn[LANG_COUNT];
static lv_obj_t *s_wifi_heading;
static lv_obj_t *s_wifi_desc;
static lv_obj_t *s_wifi_btn_label;
static lv_obj_t *s_wifi_current;

static void paint_language_selection(void)
{
    for (int i = 0; i < LANG_COUNT; i++) {
        bool active = (i == (int)i18n_get());
        lv_obj_set_style_bg_color(s_lang_btn[i], active ? UI_COL_ACCENT : UI_COL_CARD_HI, 0);
        /* Dark text on the bright accent fill, light text otherwise. */
        lv_obj_set_style_text_color(s_lang_label[i], active ? UI_COL_BG : UI_COL_TEXT, 0);
    }
}

static void on_language_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lang_t lang = (lang_t)(uintptr_t)lv_obj_get_user_data(btn);
    if (lang == i18n_get()) {
        return;
    }

    i18n_set(lang);
    paint_language_selection();
    /* Every screen holds its own labels, so they all have to be re-rendered. */
    ui_retranslate();
    ESP_LOGI(TAG, "language switched to %s", i18n_lang_name(lang));
}

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    ui_show_weather();
}

static void on_wifi_clicked(lv_event_t *e)
{
    (void)e;
    ui_show_wifi();
    ui_wifi_set_status(T(STR_SCANNING), false);
    const ui_cmd_t cmd = {.type = UI_CMD_WIFI_SCAN};
    ui_post_cmd(&cmd);
}

static lv_obj_t *make_button(lv_obj_t *parent, lv_obj_t **out_label, const char *text,
                             lv_color_t bg, int x, int y, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_ui_18, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
    lv_obj_center(l);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    if (out_label) {
        *out_label = l;
    }
    return btn;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y,
                            const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *ui_settings_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, UI_COL_BG, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    make_button(s_scr, &s_back_label, LV_SYMBOL_LEFT "  ", UI_COL_CARD_HI, 40, 20, 150, 48,
                on_back_clicked);
    s_title = make_label(s_scr, &lv_font_ui_28, UI_COL_TEXT, 210, 26, "");

    /* ---- language ---- */
    lv_obj_t *lang_card = lv_obj_create(s_scr);
    ui_style_card(lang_card);
    lv_obj_set_size(lang_card, 720, 156);
    lv_obj_set_pos(lang_card, 40, 90);

    s_lang_heading = make_label(lang_card, &lv_font_ui_20, UI_COL_TEXT, 24, 18, "");
    s_lang_desc = make_label(lang_card, &lv_font_ui_14, UI_COL_MUTED, 24, 46, "");

    for (int i = 0; i < LANG_COUNT; i++) {
        /* Endonyms are not translated, so these labels never change. */
        s_lang_btn[i] = make_button(lang_card, &s_lang_label[i], i18n_lang_name((lang_t)i),
                                    UI_COL_CARD_HI, 24 + i * 230, 84, 210, 52, on_language_clicked);
        lv_obj_set_user_data(s_lang_btn[i], (void *)(uintptr_t)i);
        lv_obj_set_style_text_font(s_lang_label[i], &lv_font_ui_20, 0);
    }

    /* ---- network ---- */
    lv_obj_t *wifi_card = lv_obj_create(s_scr);
    ui_style_card(wifi_card);
    lv_obj_set_size(wifi_card, 720, 150);
    lv_obj_set_pos(wifi_card, 40, 266);

    s_wifi_heading = make_label(wifi_card, &lv_font_ui_20, UI_COL_TEXT, 24, 18, "");
    s_wifi_desc = make_label(wifi_card, &lv_font_ui_14, UI_COL_MUTED, 24, 46, "");
    make_button(wifi_card, &s_wifi_btn_label, "", UI_COL_ACCENT, 24, 82, 320, 52, on_wifi_clicked);
    lv_obj_set_style_text_color(s_wifi_btn_label, UI_COL_BG, 0);

    s_wifi_current = make_label(wifi_card, &lv_font_ui_16, UI_COL_MUTED, 368, 98, "");

    ui_settings_retranslate();
    return s_scr;
}

void ui_settings_retranslate(void)
{
    if (!s_scr) {
        return;
    }
    lv_label_set_text(s_title, T(STR_SETTINGS));
    lv_label_set_text_fmt(s_back_label, LV_SYMBOL_LEFT "  %s", T(STR_BACK));

    lv_label_set_text(s_lang_heading, T(STR_LANGUAGE));
    lv_label_set_text(s_lang_desc, T(STR_LANGUAGE_DESC));

    lv_label_set_text(s_wifi_heading, T(STR_WIFI));
    lv_label_set_text(s_wifi_desc, T(STR_WIFI_DESC));
    lv_label_set_text(s_wifi_btn_label, T(STR_CHOOSE_NETWORK));

    if (wifi_mgr_get_state() == WIFI_MGR_CONNECTED) {
        lv_label_set_text_fmt(s_wifi_current, LV_SYMBOL_WIFI "  %s", wifi_mgr_current_ssid());
    } else {
        lv_label_set_text_fmt(s_wifi_current, LV_SYMBOL_WARNING "  %s", T(STR_OFFLINE));
    }

    paint_language_selection();
}
