#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "net/holidays.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "ui/ui_priv.h"
#include "ui/weather_icon.h"

static const char *TAG = "ui.weather";

/* ---- Layout ---------------------------------------------------------------
 * 800x480 with a 12px outer margin:
 *   header    y   0 .. 62   city | clock | link + buttons
 *   current  y  64 .. 260
 *   forecast y 270 .. 466   (7 cards, 104 wide, 8px gutters = 776)
 */
#define MARGIN     12
#define SCREEN_W   800
#define CURRENT_Y  64
#define CURRENT_H  196
#define FORECAST_Y 270
#define FORECAST_H 196
#define CARD_W     104
#define CARD_GAP   8
#define HERO_ICON  128
#define DAY_ICON   48
#define DAY_ICON_Y 46

/* Right-hand edge of the text block, left of the two header buttons. */
#define HEADER_TEXT_RIGHT 150

#define STAT_COUNT 6

/* A clock that is merely near-right is worse than none: only show a time once
 * SNTP has actually set the system clock (any value past 2020 proves it). */
#define CLOCK_VALID_AFTER 1577836800 /* 2020-01-01 UTC */

static lv_obj_t *s_scr;

static lv_obj_t *s_city;
static lv_obj_t *s_region;
static lv_obj_t *s_clock;
static lv_obj_t *s_date;
static lv_obj_t *s_link;
static lv_obj_t *s_updated;
static lv_obj_t *s_refresh_btn;

static lv_obj_t *s_current_card;
static lv_obj_t *s_icon; /* rebuilt on every update — it is a tree of shapes */
static lv_obj_t *s_temp;
static lv_obj_t *s_condition;
static lv_obj_t *s_feels;
static lv_obj_t *s_stat_name[STAT_COUNT];
static lv_obj_t *s_stat_value[STAT_COUNT];

static lv_obj_t *s_day_card[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_name[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_date[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_icon[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_max[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_min[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_precip[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_holiday[WEATHER_MAX_DAYS];
static lv_obj_t *s_day_bar[WEATHER_MAX_DAYS];

/* Last readings, kept so a language change can re-render without a refetch. */
static weather_data_t s_data;
static geo_location_t s_loc;
static bool s_has_data;
static int s_utc_offset;

static const str_id_t k_stat_names[STAT_COUNT] = {
    STR_STAT_FEELS, STR_STAT_HUMIDITY, STR_STAT_WIND,
    STR_STAT_PRESSURE, STR_STAT_PRECIP, STR_STAT_SUN,
};

/** @brief "2026-07-31T14:00" -> "14:00", or "--:--" if unparseable. */
static const char *time_of_day(const char *iso, char *buf, size_t buf_sz)
{
    const char *t = iso ? strchr(iso, 'T') : NULL;
    if (t && strlen(t) >= 6) {
        snprintf(buf, buf_sz, "%.5s", t + 1);
    } else {
        snprintf(buf, buf_sz, "--:--");
    }
    return buf;
}

/** @brief "2026-07-31" -> "31.07" or "31.07.2026". */
static const char *eu_date(const char *iso, char *buf, size_t buf_sz, bool with_year)
{
    int y, m, d;
    if (iso && sscanf(iso, "%d-%d-%d", &y, &m, &d) == 3) {
        if (with_year) {
            snprintf(buf, buf_sz, "%02d.%02d.%04d", d, m, y);
        } else {
            snprintf(buf, buf_sz, "%02d.%02d", d, m);
        }
    } else {
        snprintf(buf, buf_sz, with_year ? "--.--.----" : "--.--");
    }
    return buf;
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

/* ---- clock --------------------------------------------------------------- */

/**
 * @brief Repaint the header clock and date.
 *
 * SNTP keeps the system clock in UTC; rather than carry a timezone database we
 * add the offset Open-Meteo reports for the location. That value already has
 * the current DST state folded in, so it stays right across the changeover.
 */
static void refresh_clock(void)
{
    time_t utc = time(NULL);
    if (utc < CLOCK_VALID_AFTER) {
        lv_label_set_text(s_clock, "--:--");
        if (s_has_data && s_data.day_count > 0) {
            char buf[16];
            lv_label_set_text(s_date, eu_date(s_data.days[0].date, buf, sizeof(buf), true));
        }
        return;
    }

    time_t local = utc + s_utc_offset;
    struct tm tm;
    gmtime_r(&local, &tm);

    lv_label_set_text_fmt(s_clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(s_date, "%02d.%02d.%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh_clock();
}

/* ---- events -------------------------------------------------------------- */

static void on_refresh_clicked(lv_event_t *e)
{
    (void)e;
    const ui_cmd_t cmd = {.type = UI_CMD_REFRESH};
    ui_post_cmd(&cmd);
}

static void on_settings_clicked(lv_event_t *e)
{
    (void)e;
    ui_show_settings();
}

static lv_obj_t *make_icon_button(lv_obj_t *parent, const char *symbol, int x, int y,
                                  lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 52, 40);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, UI_COL_CARD, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_font(l, &lv_font_ui_18, 0);
    lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
    lv_obj_center(l);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ---- construction -------------------------------------------------------- */

static void build_header(void)
{
    s_city = make_label(s_scr, &lv_font_ui_28, UI_COL_TEXT, MARGIN + 12, 4, "--");
    s_region = make_label(s_scr, &lv_font_ui_14, UI_COL_MUTED, MARGIN + 12, 38, "");

    /* Clock centred between the location block and the status block. */
    s_clock = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_clock, &lv_font_ui_28, 0);
    lv_obj_set_style_text_color(s_clock, UI_COL_TEXT, 0);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 4);

    s_date = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_date, &lv_font_ui_14, 0);
    lv_obj_set_style_text_color(s_date, UI_COL_MUTED, 0);
    lv_label_set_text(s_date, "");
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 40);

    s_link = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_link, &lv_font_ui_16, 0);
    lv_obj_set_style_text_color(s_link, UI_COL_MUTED, 0);
    lv_label_set_text(s_link, LV_SYMBOL_WIFI);
    lv_obj_align(s_link, LV_ALIGN_TOP_RIGHT, -HEADER_TEXT_RIGHT, 6);

    s_updated = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_updated, &lv_font_ui_12, 0);
    lv_obj_set_style_text_color(s_updated, UI_COL_MUTED, 0);
    lv_label_set_text(s_updated, "");
    lv_obj_align(s_updated, LV_ALIGN_TOP_RIGHT, -HEADER_TEXT_RIGHT, 34);

    s_refresh_btn = make_icon_button(s_scr, LV_SYMBOL_REFRESH, 664, 10, on_refresh_clicked);
    make_icon_button(s_scr, LV_SYMBOL_SETTINGS, 724, 10, on_settings_clicked);
}

static void build_current_card(void)
{
    s_current_card = lv_obj_create(s_scr);
    ui_style_card(s_current_card);
    lv_obj_set_size(s_current_card, SCREEN_W - 2 * MARGIN, CURRENT_H);
    lv_obj_set_pos(s_current_card, MARGIN, CURRENT_Y);

    s_icon = weather_icon_create(s_current_card, WICON_CLOUD, HERO_ICON, true);
    lv_obj_set_pos(s_icon, 24, 34);

    s_temp = make_label(s_current_card, &lv_font_ui_48, UI_COL_TEXT, 180, 34, "--°");
    s_condition = make_label(s_current_card, &lv_font_ui_24, UI_COL_ACCENT, 180, 98, "--");
    s_feels = make_label(s_current_card, &lv_font_ui_16, UI_COL_MUTED, 180, 134, "");

    /* Two rows of three stats filling the right half of the card. */
    for (int i = 0; i < STAT_COUNT; i++) {
        int x = 412 + (i % 3) * 122;
        int y = 34 + (i / 3) * 72;
        s_stat_name[i] = make_label(s_current_card, &lv_font_ui_12, UI_COL_MUTED, x, y, "");
        s_stat_value[i] = make_label(s_current_card, &lv_font_ui_18, UI_COL_TEXT, x, y + 20, "--");
    }
}

static void build_forecast(void)
{
    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        int x = MARGIN + i * (CARD_W + CARD_GAP);

        lv_obj_t *card = lv_obj_create(s_scr);
        ui_style_card(card);
        lv_obj_set_size(card, CARD_W, FORECAST_H);
        lv_obj_set_pos(card, x, FORECAST_Y);
        s_day_card[i] = card;

        /* Accent stripe along the top edge, shown only on days off. */
        s_day_bar[i] = lv_obj_create(card);
        lv_obj_remove_style_all(s_day_bar[i]);
        lv_obj_set_size(s_day_bar[i], CARD_W, 4);
        lv_obj_set_pos(s_day_bar[i], 0, 0);
        lv_obj_set_style_bg_opa(s_day_bar[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(s_day_bar[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_day_bar[i], LV_OBJ_FLAG_HIDDEN);

        s_day_name[i] = make_label(card, &lv_font_ui_18, UI_COL_TEXT, 0, 10, "--");
        s_day_date[i] = make_label(card, &lv_font_ui_12, UI_COL_MUTED, 0, 31, "");
        s_day_max[i] = make_label(card, &lv_font_ui_20, UI_COL_TEXT, 0, 100, "--°");
        s_day_min[i] = make_label(card, &lv_font_ui_16, UI_COL_MUTED, 0, 124, "--°");
        s_day_precip[i] = make_label(card, &lv_font_ui_14, UI_COL_COOL, 0, 146, "");

        /* Holiday names run long ("Christi Himmelfahrt"), so this one wraps
         * across the two lines left at the bottom of the card. */
        s_day_holiday[i] = make_label(card, &lv_font_ui_12, UI_COL_WARM, 0, 166, "");
        lv_label_set_long_mode(s_day_holiday[i], LV_LABEL_LONG_WRAP);

        lv_obj_t *centred[] = {s_day_name[i], s_day_date[i], s_day_max[i], s_day_min[i],
                               s_day_precip[i], s_day_holiday[i]};
        for (size_t k = 0; k < sizeof(centred) / sizeof(centred[0]); k++) {
            lv_obj_set_width(centred[k], CARD_W);
            lv_obj_set_style_text_align(centred[k], LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_set_width(s_day_holiday[i], CARD_W - 8);
        lv_obj_set_pos(s_day_holiday[i], 4, 166);

        /* Static: seven animated icons would keep most of the screen
         * invalidating, and that redraw traffic competes with the LCD DMA. */
        s_day_icon[i] = weather_icon_create(card, WICON_CLOUD, DAY_ICON, false);
        lv_obj_set_pos(s_day_icon[i], (CARD_W - DAY_ICON) / 2, DAY_ICON_Y);
    }
}

lv_obj_t *ui_weather_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, UI_COL_BG, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header();
    build_current_card();
    build_forecast();

    lv_timer_create(clock_timer_cb, 1000, NULL);
    ui_weather_retranslate();
    return s_scr;
}

/* ---- rendering ----------------------------------------------------------- */

/** @brief Draw everything from the stored readings. */
static void render(void)
{
    for (int i = 0; i < STAT_COUNT; i++) {
        lv_label_set_text(s_stat_name[i], T(k_stat_names[i]));
    }

    if (!s_has_data) {
        return;
    }

    lv_label_set_text(s_city, s_loc.city[0] ? s_loc.city : T(STR_UNKNOWN_PLACE));
    if (s_loc.region[0] && s_loc.country[0]) {
        lv_label_set_text_fmt(s_region, "%s, %s", s_loc.region, s_loc.country);
    } else {
        lv_label_set_text(s_region, s_loc.country);
    }

    const weather_current_t *c = &s_data.current;

    /* The icon is a tree of shapes, so a change of conditions means a rebuild.
     * Deleting it also stops the animations attached to it. */
    lv_obj_delete(s_icon);
    s_icon = weather_icon_create(s_current_card, weather_code_icon(c->code, c->is_day), HERO_ICON,
                                 true);
    lv_obj_set_pos(s_icon, 24, 34);

    lv_label_set_text_fmt(s_temp, "%d°", (int)lroundf(c->temp_c));
    lv_label_set_text(s_condition, T(i18n_wmo_string(c->code)));
    lv_label_set_text_fmt(s_feels, T(STR_FEELS_LIKE_FMT), (int)lroundf(c->feels_c));

    lv_label_set_text_fmt(s_stat_value[0], "%d°C", (int)lroundf(c->feels_c));
    lv_label_set_text_fmt(s_stat_value[1], "%d %%", (int)lroundf(c->humidity_pct));
    lv_label_set_text_fmt(s_stat_value[2], "%.1f %s", c->wind_ms, T(STR_UNIT_MS));
    lv_label_set_text_fmt(s_stat_value[3], "%d %s", (int)lroundf(c->pressure_hpa), T(STR_UNIT_HPA));
    lv_label_set_text_fmt(s_stat_value[4], "%.1f %s", c->precip_mm, T(STR_UNIT_MM));

    if (s_data.day_count > 0) {
        char rise[8];
        char set[8];
        time_of_day(s_data.days[0].sunrise, rise, sizeof(rise));
        time_of_day(s_data.days[0].sunset, set, sizeof(set));
        lv_label_set_text_fmt(s_stat_value[5], "%s / %s", rise, set);
    } else {
        lv_label_set_text(s_stat_value[5], "--");
    }

    char hhmm[8];
    lv_label_set_text_fmt(s_updated, T(STR_UPDATED), time_of_day(c->time, hhmm, sizeof(hhmm)));

    for (int i = 0; i < WEATHER_MAX_DAYS; i++) {
        if (i >= s_data.day_count) {
            lv_obj_add_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_day_card[i], LV_OBJ_FLAG_HIDDEN);
        const weather_day_t *day = &s_data.days[i];

        lv_label_set_text(s_day_name[i], i == 0 ? T(STR_TODAY) : i18n_weekday(day->date));

        char datebuf[12];
        lv_label_set_text(s_day_date[i], eu_date(day->date, datebuf, sizeof(datebuf), false));

        /*
         * Mark the days off. A public holiday outranks the weekend: it is the
         * more interesting fact, and it carries a name to show.
         */
        const char *holiday = holidays_name(day->date);
        int dow = i18n_weekday_index(day->date);
        lv_color_t name_col = UI_COL_TEXT;
        bool show_bar = false;
        lv_color_t bar_col = UI_COL_DAYOFF;

        if (holiday) {
            name_col = UI_COL_DAYOFF;
            bar_col = UI_COL_DAYOFF;
            show_bar = true;
        } else if (dow == 0) { /* Sunday */
            name_col = UI_COL_DAYOFF;
            bar_col = UI_COL_DAYOFF;
            show_bar = true;
        } else if (dow == 6) { /* Saturday */
            name_col = UI_COL_WARM;
            bar_col = UI_COL_WARM;
            show_bar = true;
        }

        lv_obj_set_style_text_color(s_day_name[i], name_col, 0);
        lv_label_set_text(s_day_holiday[i], holiday ? holiday : "");
        if (show_bar) {
            lv_obj_set_style_bg_color(s_day_bar[i], bar_col, 0);
            lv_obj_clear_flag(s_day_bar[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_day_bar[i], LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_delete(s_day_icon[i]);
        /* Daily codes carry no day/night sense — always the daytime variant. */
        s_day_icon[i] = weather_icon_create(s_day_card[i], weather_code_icon(day->code, true),
                                            DAY_ICON, false);
        lv_obj_set_pos(s_day_icon[i], (CARD_W - DAY_ICON) / 2, DAY_ICON_Y);

        lv_label_set_text_fmt(s_day_max[i], "%d°", (int)lroundf(day->tmax_c));
        lv_label_set_text_fmt(s_day_min[i], "%d°", (int)lroundf(day->tmin_c));

        if (day->precip_prob_pct > 0) {
            lv_label_set_text_fmt(s_day_precip[i], LV_SYMBOL_TINT " %d%%", day->precip_prob_pct);
        } else {
            lv_label_set_text(s_day_precip[i], "");
        }
    }

    refresh_clock();
}

void ui_weather_update(const geo_location_t *loc, const weather_data_t *d)
{
    if (!s_scr || !d) {
        return;
    }
    s_data = *d;
    if (loc) {
        s_loc = *loc;
    }
    s_utc_offset = d->utc_offset_seconds;
    s_has_data = true;

    render();
    ESP_LOGI(TAG, "screen updated (%d days)", d->day_count);
}

void ui_weather_retranslate(void)
{
    if (!s_scr) {
        return;
    }
    render();
    ui_weather_set_link(wifi_mgr_get_state(), wifi_mgr_rssi());
}

bool ui_weather_has_data(void)
{
    return s_has_data;
}

void ui_weather_set_link(wifi_mgr_state_t state, int8_t rssi)
{
    if (!s_link) {
        return;
    }
    switch (state) {
    case WIFI_MGR_CONNECTED:
        lv_label_set_text_fmt(s_link, LV_SYMBOL_WIFI "  %s  %d dBm", wifi_mgr_current_ssid(), rssi);
        lv_obj_set_style_text_color(s_link, UI_COL_MUTED, 0);
        break;
    case WIFI_MGR_CONNECTING:
        lv_label_set_text_fmt(s_link, LV_SYMBOL_WIFI "  %s", T(STR_CONNECTING_LC));
        lv_obj_set_style_text_color(s_link, UI_COL_WARM, 0);
        break;
    default:
        lv_label_set_text_fmt(s_link, LV_SYMBOL_WARNING "  %s", T(STR_OFFLINE));
        lv_obj_set_style_text_color(s_link, UI_COL_DANGER, 0);
        break;
    }
    /* Width changed, so re-anchor against the right edge. */
    lv_obj_align(s_link, LV_ALIGN_TOP_RIGHT, -HEADER_TEXT_RIGHT, 6);
}

void ui_weather_set_busy(bool busy)
{
    if (!s_refresh_btn) {
        return;
    }
    if (busy) {
        lv_obj_add_state(s_refresh_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_refresh_btn, LV_OPA_50, 0);
    } else {
        lv_obj_clear_state(s_refresh_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(s_refresh_btn, LV_OPA_COVER, 0);
    }
}
