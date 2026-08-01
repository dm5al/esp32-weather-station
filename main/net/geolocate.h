#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float latitude;
    float longitude;
    char city[48];
    char region[48];
    char country[48];
    char timezone[48]; /* IANA name, e.g. "Europe/Berlin" */
    /* ISO codes, needed to look up regional public holidays: together they
     * form the subdivision key, e.g. "DE" + "RP" -> "DE-RP" (Rheinland-Pfalz). */
    char country_code[4];
    char region_code[8];
} geo_location_t;

/**
 * @brief Work out where we are from the public IP address.
 *
 * Tries ipapi.co over HTTPS first and falls back to ip-api.com. On success the
 * result is cached in NVS, so a later provider outage does not leave the
 * station with nothing to show.
 */
esp_err_t geo_detect(geo_location_t *out);

/** @brief Load the location cached by the last successful geo_detect(). */
esp_err_t geo_load_cached(geo_location_t *out);

#ifdef __cplusplus
}
#endif
