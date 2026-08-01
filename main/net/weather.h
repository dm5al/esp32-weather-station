#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "net/geolocate.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_MAX_DAYS 7

/** @brief Visual family a WMO weather code belongs to. */
typedef enum {
    WICON_SUN,
    WICON_MOON,
    WICON_PARTLY_DAY,
    WICON_PARTLY_NIGHT,
    WICON_CLOUD,
    WICON_FOG,
    WICON_DRIZZLE,
    WICON_RAIN,
    WICON_SNOW,
    WICON_THUNDER,
} weather_icon_t;

typedef struct {
    float temp_c;
    float feels_c;
    float humidity_pct;
    float precip_mm;
    float wind_ms; /* metres per second, requested from the API as such */
    float pressure_hpa;
    int wind_dir_deg;
    int code;    /* WMO weather interpretation code */
    bool is_day;
    char time[20]; /* local ISO8601, "2026-07-31T14:00" */
} weather_current_t;

typedef struct {
    char date[12]; /* "2026-07-31" */
    int code;
    float tmax_c;
    float tmin_c;
    int precip_prob_pct;
    float wind_ms;
    char sunrise[20];
    char sunset[20];
} weather_day_t;

typedef struct {
    weather_current_t current;
    weather_day_t days[WEATHER_MAX_DAYS];
    int day_count;
    int utc_offset_seconds;
    char timezone_abbr[12];
} weather_data_t;

/**
 * @brief Fetch current conditions plus a 7-day forecast from Open-Meteo.
 *
 * Open-Meteo needs no API key. Times come back already converted to the
 * location's local zone (timezone=auto), so nothing here has to do TZ maths.
 */
esp_err_t weather_fetch(const geo_location_t *loc, weather_data_t *out);

/** @brief Icon family for a WMO code. @param is_day picks sun vs moon variants. */
weather_icon_t weather_code_icon(int code, bool is_day);

#ifdef __cplusplus
}
#endif
