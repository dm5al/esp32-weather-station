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
 * @brief Where the location comes from.
 *
 * Automatic is right for most people and needs no setup, but it is only ever as
 * good as the IP address: behind a VPN, on mobile broadband, or on a connection
 * whose registered address sits at the far end of the country, it can be tens or
 * hundreds of kilometres out. Manual exists for those cases, and for anyone who
 * would simply rather say.
 */
typedef enum {
    GEO_MODE_AUTO = 0,
    GEO_MODE_MANUAL,
} geo_mode_t;

/** @brief Current mode. Defaults to automatic when nothing is stored. */
geo_mode_t geo_get_mode(void);

/** @brief Store the mode. Takes effect at the next resolve. */
esp_err_t geo_set_mode(geo_mode_t mode);

/** @brief Store the manually chosen place and switch to manual. */
esp_err_t geo_set_manual(const geo_location_t *loc);

/** @brief The manually chosen place, if one has been set. */
esp_err_t geo_get_manual(geo_location_t *out);

/**
 * @brief Resolve the location according to the current mode.
 *
 * Manual returns the stored place without touching the network. Automatic
 * detects from the IP address. Manual with nothing stored falls back to
 * detection rather than failing, so a half-finished setting cannot leave the
 * display with nowhere to be.
 */
esp_err_t geo_resolve(geo_location_t *out);

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

/** @brief How many search results are kept. */
#define GEO_SEARCH_MAX 8

/**
 * @brief Look a place up by name with Open-Meteo's geocoder.
 *
 * Same provider as the forecast, no key, and it answers with coordinates, a
 * country and an IANA timezone — everything the rest of the program needs.
 *
 * One thing it does not return is the ISO subdivision code, so a place chosen
 * this way carries no region_code and the holiday lookup keeps only nationwide
 * entries. That is a real difference from automatic, and the honest one: better
 * a short list that is right than a regional list that is guessed.
 */
esp_err_t geo_search(const char *query, geo_location_t *out, size_t max, size_t *found);

#ifdef __cplusplus
}
#endif
