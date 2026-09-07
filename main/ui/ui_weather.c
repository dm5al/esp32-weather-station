#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/board.h" /* BSP_LCD_H_RES / BSP_LCD_V_RES: the layout is sized from them */
#include "esp_log.h"
#include "net/holidays.h"
#include "ui/fonts/ui_fonts.h"
#include "ui/i18n.h"
#include "ui/ui.h"
#include "ui/ui_priv.h"
#include "ui/weather_icon.h"

static const char *TAG = "ui.weather";

/* ---- Layout ---------------------------------------------------------------
 * A 12 px outer margin, and three bands down the screen:
 *   header    city | clock | link + buttons
 *   today     conditions, the next 24 hours, and the readings
 *   forecast  seven day cards with 8 px gutters
 *
 * At 800x480 that is header 0..62, today 64..274, forecast 286..466 with 104 px
 * cards - the sizes this was tuned at on the panel.
 *
 * Everything about the present day is one card rather than three. The first
 * attempt gave the next 24 hours a strip of its own between the current
 * conditions and the week, which meant three horizontal bands and two gutters
 * spent separating things that belong together. Folding the hours into the same
 * card as the conditions they continue from - and putting the readings along the
 * bottom of it - buys back the space those borders were using, and says
 * something truer about the content: this card is today, the row below is the
 * rest of the week.
 *
 * Sized from the panel rather than from the number 800.
 *
 * Written for an 800x480 board and then asked to run on a 1024x600 HDMI
 * monitor, where an absolute layout would have sat in the top-left corner with
 * a third of the screen unused. Everything below is therefore derived from
 * BSP_LCD_H_RES and BSP_LCD_V_RES.
 *
 * The arithmetic is arranged so that at 800x480 every value comes out exactly
 * what it was when it was tuned by hand on the panel. That is deliberate: the
 * point of deriving these is to gain a second size, not to re-litigate the
 * first. A wider screen widens the cards and their columns; a taller one gives
 * the extra height to the two cards in the same proportion they already had.
 */
#define MARGIN     12
#define SCREEN_W   BSP_LCD_H_RES
#define SCREEN_H   BSP_LCD_V_RES
#define HEADER_H   64

#define CURRENT_Y  HEADER_H
#define CURRENT_W  (SCREEN_W - 2 * MARGIN)

/* Vertical space below the header, less the outer margin and the gutter between
 * the two cards. At 480 this is 392, split 210/182 - which is what the hand
 * tuning arrived at, to within the two pixels rounding costs. */
#define BODY_H     (SCREEN_H - HEADER_H - MARGIN - CARD_GAP_V)
#define CARD_GAP_V 12
#define CURRENT_H  ((BODY_H * 210) / 392)

/*
 * The week takes whatever is left below today's card.
 *
 * Its contents are a chain with no slack: the holiday label wraps to two lines
 * and has to reach the bottom edge, which pins everything above it, and the icon
 * has to clear the date. That is why the day icon is 44 px and not the 48 it
 * started at - those 4 px are what let the icon sit clear of the date without a
 * name like "Christi Himmelfahrt" being clipped. All of it scales with the card,
 * so a taller screen relieves the squeeze rather than inheriting it.
 */
#define FORECAST_Y (CURRENT_Y + CURRENT_H + CARD_GAP_V)
#define FORECAST_H (SCREEN_H - MARGIN - FORECAST_Y)
#define CARD_GAP   8
/* Seven cards and six gutters filling the width. 104 at 800 wide. */
#define CARD_W     ((CURRENT_W - (WEATHER_MAX_DAYS - 1) * CARD_GAP) / WEATHER_MAX_DAYS)
#define HERO_ICON  CUR_V(88)

/*
 * The next 24 hours: eight columns at three-hour steps, four across and two
 * down in the right half of today's card.
 *
 * The API returns every hour and all 24 are kept, but 24 columns is 32 px each
 * - narrower than the two digits of a temperature. Three-hourly is what a
 * person plans around, and two rows of four fit beside the conditions where
 * eight in a row would not.
 */
/*
 * Vertical positions inside a card scale with that card; horizontal ones inside
 * today's card scale with its width. At the size everything was tuned at these
 * are the identity, so the 800x480 firmware is unchanged to the pixel.
 */
#define CUR_V(v) ((v) * CURRENT_H / 210)
#define CUR_X(v) ((v) * CURRENT_W / 776)
#define FC_V(v)  ((v) * FORECAST_H / 180)

#define HOURLY_SLOTS  8
#define HOURLY_COLS   4
#define HOURLY_STEP   3
#define HOURLY_ICON   CUR_V(24)
#define HOURLY_X      CUR_X(392)                           /* card-relative */
#define HOURLY_COL_W  ((CURRENT_W - HOURLY_X) / HOURLY_COLS)
#define HOURLY_ROW_H  CUR_V(68)
#define HOURLY_Y0     CUR_V(6)

/*
 * The readings, in one row along the bottom of the same card.
 *
 * Six of them, each a name over its value. They used to be two rows of three
 * stacked beside the temperature, which put them in competition with it for the
 * eye; along the bottom they read as what they are - the detail you look at
 * second.
 */
#define STATS_Y       CUR_V(150)
#define STAT_COL_W    (CURRENT_W / STAT_COUNT)

#define DAY_ICON   FC_V(44)
#define DAY_ICON_Y FC_V(44)

/* Right-hand edge of the text block, left of the two header buttons. */
#define HEADER_TEXT_RIGHT 150
#define HEADER_BTN_W      52

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
static lv_obj_t *s_stat_name[STAT_COUNT];
static lv_obj_t *s_stat_value[STAT_COUNT];

static lv_obj_t *s_hour_card;
static lv_obj_t *s_hour_time[HOURLY_SLOTS];
static lv_obj_t *s_hour_icon[HOURLY_SLOTS];
static lv_obj_t *s_hour_temp[HOURLY_SLOTS];
static lv_obj_t *s_hour_precip[HOURLY_SLOTS];

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

    /* Anchored to the right edge rather than to 800. */
    const int settings_x = SCREEN_W - MARGIN - 12 - HEADER_BTN_W;
    s_refresh_btn =
        make_icon_button(s_scr, LV_SYMBOL_REFRESH, settings_x - 60, 10, on_refresh_clicked);
    make_icon_button(s_scr, LV_SYMBOL_SETTINGS, settings_x, 10, on_settings_clicked);
}

static void build_current_card(void)
{
    s_current_card = lv_obj_create(s_scr);
    ui_style_card(s_current_card);
    lv_obj_set_size(s_current_card, SCREEN_W - 2 * MARGIN, CURRENT_H);
    lv_obj_set_pos(s_current_card, MARGIN, CURRENT_Y);

    s_icon = weather_icon_create(s_current_card, WICON_CLOUD, HERO_ICON, true);
    lv_obj_set_pos(s_icon, CUR_X(24), CUR_V(18));

    s_temp = make_label(s_current_card, &lv_font_ui_48, UI_COL_TEXT, CUR_X(120), CUR_V(12), "--°");
    s_condition = make_label(s_current_card, &lv_font_ui_24, UI_COL_ACCENT, CUR_X(120), CUR_V(72), "--");
    /* "Feels like" is one of the six readings along the bottom now, so the
     * standalone line that used to sit here is gone: it would have said the
     * same thing twice in one card. */

    /* One row of six along the bottom: name over value, evenly spaced. */
    for (int i = 0; i < STAT_COUNT; i++) {
        int x = i * STAT_COL_W;
        s_stat_name[i] = make_label(s_current_card, &lv_font_ui_12, UI_COL_MUTED, x, STATS_Y, "");
        s_stat_value[i] =
            make_label(s_current_card, &lv_font_ui_18, UI_COL_TEXT, x, STATS_Y + CUR_V(20), "--");

        /* Centred in their column so the six read as a row rather than as six
         * left-aligned fragments of different lengths. */
        lv_obj_t *centred[] = {s_stat_name[i], s_stat_value[i]};
        for (size_t k = 0; k < sizeof(centred) / sizeof(centred[0]); k++) {
            lv_obj_set_width(centred[k], STAT_COL_W);
            lv_obj_set_style_text_align(centred[k], LV_TEXT_ALIGN_CENTER, 0);
        }
    }
}

/*
 * The next 24 hours, as one card of eight columns.
 *
 * One card rather than eight, because eight separate cards at this height would
 * be mostly border. The columns are marked out by their contents alone, which is
 * enough when every one has the same three lines in the same places.
 */
static void build_hourly(void)
{
    /* No card of its own: these live in the right half of today's card, so the
     * hours read as a continuation of the conditions beside them. */
    s_hour_card = s_current_card;

    for (int i = 0; i < HOURLY_SLOTS; i++) {
        const int x = HOURLY_X + (i % HOURLY_COLS) * HOURLY_COL_W;
        const int y = HOURLY_Y0 + (i / HOURLY_COLS) * HOURLY_ROW_H;

        s_hour_time[i] = make_label(s_hour_card, &lv_font_ui_12, UI_COL_MUTED, x, y, "--:--");
        s_hour_temp[i] = make_label(s_hour_card, &lv_font_ui_16, UI_COL_TEXT, x, y + CUR_V(42), "--°");
        /* Empty below the threshold, so a dry day shows nothing rather than a
         * grid of zeroes. */
        s_hour_precip[i] =
            make_label(s_hour_card, &lv_font_ui_12, UI_COL_COOL,
                       x + HOURLY_COL_W / 2 + CUR_X(14), y + CUR_V(44), "");

        lv_obj_t *centred[] = {s_hour_time[i], s_hour_temp[i]};
        for (size_t k = 0; k < sizeof(centred) / sizeof(centred[0]); k++) {
            lv_obj_set_width(centred[k], HOURLY_COL_W);
            lv_obj_set_style_text_align(centred[k], LV_TEXT_ALIGN_CENTER, 0);
        }

        s_hour_icon[i] = weather_icon_create(s_hour_card, WICON_CLOUD, HOURLY_ICON, false);
        lv_obj_set_pos(s_hour_icon[i], x + (HOURLY_COL_W - HOURLY_ICON) / 2, y + CUR_V(16));
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

        s_day_name[i] = make_label(card, &lv_font_ui_18, UI_COL_TEXT, 0, FC_V(2), "--");
        s_day_date[i] = make_label(card, &lv_font_ui_12, UI_COL_MUTED, 0, FC_V(24), "");
        s_day_max[i] = make_label(card, &lv_font_ui_20, UI_COL_TEXT, 0, FC_V(90), "--°");
        s_day_min[i] = make_label(card, &lv_font_ui_16, UI_COL_MUTED, 0, FC_V(114), "--°");
        s_day_precip[i] = make_label(card, &lv_font_ui_14, UI_COL_COOL, 0, FC_V(132), "");

        /* Holiday names run long ("Christi Himmelfahrt"), so this one wraps
         * across the two lines left at the bottom of the card. */
        s_day_holiday[i] = make_label(card, &lv_font_ui_12, UI_COL_WARM, 0, FC_V(150), "");
        lv_label_set_long_mode(s_day_holiday[i], LV_LABEL_LONG_WRAP);

        lv_obj_t *centred[] = {s_day_name[i], s_day_date[i], s_day_max[i], s_day_min[i],
                               s_day_precip[i], s_day_holiday[i]};
        for (size_t k = 0; k < sizeof(centred) / sizeof(centred[0]); k++) {
            lv_obj_set_width(centred[k], CARD_W);
            lv_obj_set_style_text_align(centred[k], LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_obj_set_width(s_day_holiday[i], CARD_W - 8);
        lv_obj_set_pos(s_day_holiday[i], 4, FC_V(150));

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
    build_hourly();
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
    lv_obj_set_pos(s_icon, 24, 18);

    lv_label_set_text_fmt(s_temp, "%d°", (int)lroundf(c->temp_c));
    lv_label_set_text(s_condition, T(i18n_wmo_string(c->code)));

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

    /*
     * The next 24 hours, in the right half of this same card.
     *
     * Each slot is cleared individually rather than hiding a container: the
     * container is now today's card, and hiding it would take the temperature
     * and the readings with it. An hour with no data shows nothing at all —
     * dashes would suggest a flat forecast rather than an absent one.
     */
    for (int i = 0; i < HOURLY_SLOTS; i++) {
        const int idx = i * HOURLY_STEP;
        const bool have = idx < s_data.hour_count;
        const weather_hour_t *h = have ? &s_data.hours[idx] : NULL;

        if (!have) {
            lv_label_set_text(s_hour_time[i], "");
            lv_label_set_text(s_hour_temp[i], "");
            lv_label_set_text(s_hour_precip[i], "");
            lv_obj_add_flag(s_hour_icon[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        char buf[8];
        lv_label_set_text(s_hour_time[i], time_of_day(h->time, buf, sizeof(buf)));
        lv_label_set_text_fmt(s_hour_temp[i], "%d°", (int)lroundf(h->temp_c));

        /* Below a coin toss the number says little and the slot is quieter
         * without it. */
        if (h->precip_prob_pct >= 50.0f) {
            lv_label_set_text_fmt(s_hour_precip[i], "%d%%", (int)lroundf(h->precip_prob_pct));
        } else {
            lv_label_set_text(s_hour_precip[i], "");
        }

        /* Rebuilt like the day icons: the icon is a tree of shapes, so the
         * family cannot be changed in place. */
        lv_obj_delete(s_hour_icon[i]);
        s_hour_icon[i] = weather_icon_create(s_hour_card, weather_code_icon(h->code, h->is_day),
                                             HOURLY_ICON, false);
        lv_obj_set_pos(s_hour_icon[i],
                       HOURLY_X + (i % HOURLY_COLS) * HOURLY_COL_W +
                           (HOURLY_COL_W - HOURLY_ICON) / 2,
                       HOURLY_Y0 + (i / HOURLY_COLS) * HOURLY_ROW_H + CUR_V(16));
    }

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
