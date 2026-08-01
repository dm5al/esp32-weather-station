#include "net/weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net/http_get.h"

static const char *TAG = "weather";

#define OPEN_METEO_URL_FMT                                                            \
    "https://api.open-meteo.com/v1/forecast"                                          \
    "?latitude=%.4f&longitude=%.4f"                                                   \
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"       \
    "precipitation,weather_code,wind_speed_10m,wind_direction_10m,surface_pressure"   \
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,"                      \
    "precipitation_probability_max,wind_speed_10m_max,sunrise,sunset"                 \
    /* Ask the API for m/s directly rather than converting from its km/h
     * default — one less place for a unit mistake to hide. */                        \
    "&wind_speed_unit=ms"                                                             \
    "&timezone=auto&forecast_days=%d"

static float json_num(const cJSON *obj, const char *key, float fallback)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(n) ? (float)n->valuedouble : fallback;
}

static void json_str(const cJSON *obj, const char *key, char *dst, size_t dst_sz)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(n) && n->valuestring) {
        strlcpy(dst, n->valuestring, dst_sz);
    } else {
        dst[0] = '\0';
    }
}

/* Pull element @p idx out of a daily array, tolerating nulls and short arrays. */
static float daily_num(const cJSON *daily, const char *key, int idx, float fallback)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(daily, key);
    if (!cJSON_IsArray(arr)) {
        return fallback;
    }
    const cJSON *item = cJSON_GetArrayItem(arr, idx);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static void daily_str(const cJSON *daily, const char *key, int idx, char *dst, size_t dst_sz)
{
    dst[0] = '\0';
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(daily, key);
    if (!cJSON_IsArray(arr)) {
        return;
    }
    const cJSON *item = cJSON_GetArrayItem(arr, idx);
    if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(dst, item->valuestring, dst_sz);
    }
}

static esp_err_t parse_forecast(const char *body, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(body);
    ESP_RETURN_ON_FALSE(root, ESP_ERR_INVALID_RESPONSE, TAG, "bad JSON");

    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    memset(out, 0, sizeof(*out));

    out->utc_offset_seconds = (int)json_num(root, "utc_offset_seconds", 0);
    json_str(root, "timezone_abbreviation", out->timezone_abbr, sizeof(out->timezone_abbr));

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (!cJSON_IsObject(cur)) {
        ESP_LOGE(TAG, "response has no 'current' block");
        goto done;
    }
    out->current.temp_c = json_num(cur, "temperature_2m", 0);
    out->current.feels_c = json_num(cur, "apparent_temperature", out->current.temp_c);
    out->current.humidity_pct = json_num(cur, "relative_humidity_2m", 0);
    out->current.precip_mm = json_num(cur, "precipitation", 0);
    out->current.wind_ms = json_num(cur, "wind_speed_10m", 0);
    out->current.wind_dir_deg = (int)json_num(cur, "wind_direction_10m", 0);
    out->current.pressure_hpa = json_num(cur, "surface_pressure", 0);
    out->current.code = (int)json_num(cur, "weather_code", 0);
    out->current.is_day = json_num(cur, "is_day", 1) != 0;
    json_str(cur, "time", out->current.time, sizeof(out->current.time));

    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (cJSON_IsObject(daily)) {
        const cJSON *dates = cJSON_GetObjectItemCaseSensitive(daily, "time");
        int n = cJSON_IsArray(dates) ? cJSON_GetArraySize(dates) : 0;
        if (n > WEATHER_MAX_DAYS) {
            n = WEATHER_MAX_DAYS;
        }
        for (int i = 0; i < n; i++) {
            weather_day_t *d = &out->days[i];
            daily_str(daily, "time", i, d->date, sizeof(d->date));
            d->code = (int)daily_num(daily, "weather_code", i, 0);
            d->tmax_c = daily_num(daily, "temperature_2m_max", i, 0);
            d->tmin_c = daily_num(daily, "temperature_2m_min", i, 0);
            d->precip_prob_pct = (int)daily_num(daily, "precipitation_probability_max", i, 0);
            d->wind_ms = daily_num(daily, "wind_speed_10m_max", i, 0);
            daily_str(daily, "sunrise", i, d->sunrise, sizeof(d->sunrise));
            daily_str(daily, "sunset", i, d->sunset, sizeof(d->sunset));
        }
        out->day_count = n;
    }

    err = ESP_OK;

done:
    cJSON_Delete(root);
    return err;
}

esp_err_t weather_fetch(const geo_location_t *loc, weather_data_t *out)
{
    ESP_RETURN_ON_FALSE(loc && out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    char url[640];
    int n = snprintf(url, sizeof(url), OPEN_METEO_URL_FMT, loc->latitude, loc->longitude,
                     WEATHER_MAX_DAYS);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof(url), ESP_ERR_INVALID_SIZE, TAG, "url too long");

    char *body = NULL;
    ESP_RETURN_ON_ERROR(http_get_body(url, &body, NULL), TAG, "fetch failed");

    esp_err_t err = parse_forecast(body, out);
    free(body);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%.1f C, code %d, %d forecast days", out->current.temp_c, out->current.code,
                 out->day_count);
    }
    return err;
}

weather_icon_t weather_code_icon(int code, bool is_day)
{
    switch (code) {
    case 0:
    case 1:
        return is_day ? WICON_SUN : WICON_MOON;
    case 2:
        return is_day ? WICON_PARTLY_DAY : WICON_PARTLY_NIGHT;
    case 3:
        return WICON_CLOUD;
    case 45:
    case 48:
        return WICON_FOG;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
        return WICON_DRIZZLE;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
        return WICON_RAIN;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
        return WICON_SNOW;
    case 95:
    case 96:
    case 99:
        return WICON_THUNDER;
    default:
        return WICON_CLOUD;
    }
}
