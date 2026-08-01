#include "ui/i18n.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "i18n";

#define NVS_NS   "ui"
#define NVS_KEY  "lang"

/* One row per string, columns in lang_t order — generated from the same table
 * as str_id_t, so the two cannot drift apart. */
#define I18N_ROW(id, en, ru, de) [id] = {en, ru, de},
static const char *const k_strings[STR_COUNT][LANG_COUNT] = {I18N_STRINGS(I18N_ROW)};
#undef I18N_ROW

static lang_t s_lang = LANG_EN;

const char *T(str_id_t id)
{
    if (id >= STR_COUNT) {
        return "";
    }
    const char *s = k_strings[id][s_lang];
    /* Fall back to English rather than showing an empty label if a translation
     * is ever left out. */
    return s ? s : k_strings[id][LANG_EN];
}

void i18n_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return; /* nothing saved yet — stay on English */
    }
    uint8_t stored = LANG_EN;
    if (nvs_get_u8(h, NVS_KEY, &stored) == ESP_OK && stored < LANG_COUNT) {
        s_lang = (lang_t)stored;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "language: %s", i18n_lang_name(s_lang));
}

lang_t i18n_get(void)
{
    return s_lang;
}

esp_err_t i18n_set(lang_t lang)
{
    if (lang >= LANG_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_lang = lang;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, NVS_KEY, (uint8_t)lang);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    ESP_LOGI(TAG, "language set to %s", i18n_lang_name(lang));
    return err;
}

const char *i18n_lang_name(lang_t lang)
{
    /* Endonyms: a language is easiest to find written in itself. */
    switch (lang) {
    case LANG_EN: return "English";
    case LANG_RU: return "Русский";
    case LANG_DE: return "Deutsch";
    default:      return "?";
    }
}

str_id_t i18n_wmo_string(int code)
{
    switch (code) {
    case 0:  return STR_WMO_CLEAR;
    case 1:  return STR_WMO_MAINLY_CLEAR;
    case 2:  return STR_WMO_PARTLY;
    case 3:  return STR_WMO_OVERCAST;
    case 45: return STR_WMO_FOG;
    case 48: return STR_WMO_RIME_FOG;
    case 51: return STR_WMO_DRIZZLE_L;
    case 53: return STR_WMO_DRIZZLE;
    case 55: return STR_WMO_DRIZZLE_D;
    case 56:
    case 57: return STR_WMO_DRIZZLE_F;
    case 61: return STR_WMO_RAIN_L;
    case 63: return STR_WMO_RAIN;
    case 65: return STR_WMO_RAIN_H;
    case 66:
    case 67: return STR_WMO_RAIN_F;
    case 71: return STR_WMO_SNOW_L;
    case 73: return STR_WMO_SNOW;
    case 75: return STR_WMO_SNOW_H;
    case 77: return STR_WMO_SNOW_GRAINS;
    case 80: return STR_WMO_SHOWERS_L;
    case 81: return STR_WMO_SHOWERS;
    case 82: return STR_WMO_SHOWERS_V;
    case 85: return STR_WMO_SNOW_SHOWER;
    case 86: return STR_WMO_SNOW_SHOWER_H;
    case 95: return STR_WMO_THUNDER;
    case 96:
    case 99: return STR_WMO_THUNDER_HAIL;
    default: return STR_WMO_UNKNOWN;
    }
}

int i18n_weekday_index(const char *iso_date)
{
    int y, m, d;
    if (!iso_date || sscanf(iso_date, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) {
        return -1;
    }
    /* Sakamoto's algorithm — no mktime, no timezone state. */
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

const char *i18n_weekday(const char *iso_date)
{
    static const str_id_t names[] = {STR_DOW_SUN, STR_DOW_MON, STR_DOW_TUE, STR_DOW_WED,
                                     STR_DOW_THU, STR_DOW_FRI, STR_DOW_SAT};
    int dow = i18n_weekday_index(iso_date);
    return dow < 0 ? "" : T(names[dow]);
}
