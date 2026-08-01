/*
 * Public holidays for the detected location.
 *
 * Backed by date.nager.at, which is free, keyless and covers most of Europe.
 * German holidays are largely a Land matter — Fronleichnam is a holiday in
 * Rheinland-Pfalz but an ordinary working day in Niedersachsen — so entries are
 * filtered by the ISO subdivision code from geolocation ("DE-RP"). Nationwide
 * holidays carry no county list and always apply.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "net/geolocate.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Enough for two years of one German Land with room to spare. */
#define HOLIDAYS_MAX      48
#define HOLIDAY_NAME_MAX  40

/**
 * @brief Make sure holidays for @p year are loaded, fetching them if not.
 *
 * Cheap to call repeatedly: a year already held is a no-op. Results live in RAM
 * only — they are small, and a refetch on boot costs one request.
 *
 * @return ESP_OK if the year is available (already cached or newly fetched).
 */
esp_err_t holidays_ensure(const geo_location_t *loc, int year);

/**
 * @brief Name of the holiday falling on @p iso_date ("YYYY-MM-DD").
 *
 * The name is the local one — "Christi Himmelfahrt", not "Ascension Day" —
 * since that is what a calendar for the region would print.
 *
 * @return The name, or NULL if that date is not a public holiday.
 */
const char *holidays_name(const char *iso_date);

/** @brief Drop everything, e.g. after the location changes. */
void holidays_clear(void);

#ifdef __cplusplus
}
#endif
