#include "net/holidays.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net/http_get.h"

static const char *TAG = "holidays";

#define NAGER_URL_FMT "https://date.nager.at/api/v3/PublicHolidays/%d/%s"

/* At most two years are ever in view: the current one and, near New Year, the
 * next. A third would mean the forecast window spans 13 months. */
#define MAX_YEARS 2

typedef struct {
    char date[12];
    char name[HOLIDAY_NAME_MAX];
} holiday_t;

static holiday_t s_items[HOLIDAYS_MAX];
static int s_count;
static int s_years[MAX_YEARS];
static int s_year_count;

static bool year_loaded(int year)
{
    for (int i = 0; i < s_year_count; i++) {
        if (s_years[i] == year) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Does this entry apply to our subdivision?
 *
 * `counties` absent or null means nationwide. Otherwise it is a list like
 * ["DE-BW","DE-BY"] and we must appear in it.
 */
static bool applies_here(const cJSON *entry, const char *subdivision)
{
    const cJSON *counties = cJSON_GetObjectItemCaseSensitive(entry, "counties");
    if (!cJSON_IsArray(counties)) {
        return true; /* nationwide */
    }
    /* Without a subdivision code we cannot tell, so keep only nationwide ones
     * rather than claiming holidays that may not apply. */
    if (!subdivision || !subdivision[0]) {
        return false;
    }
    const cJSON *c = NULL;
    cJSON_ArrayForEach(c, counties)
    {
        if (cJSON_IsString(c) && c->valuestring && strcmp(c->valuestring, subdivision) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t parse_and_store(const char *body, const char *subdivision)
{
    cJSON *root = cJSON_Parse(body);
    ESP_RETURN_ON_FALSE(root, ESP_ERR_INVALID_RESPONSE, TAG, "bad JSON");
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int added = 0;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, root)
    {
        if (s_count >= HOLIDAYS_MAX) {
            ESP_LOGW(TAG, "holiday table full at %d entries", HOLIDAYS_MAX);
            break;
        }
        const cJSON *date = cJSON_GetObjectItemCaseSensitive(entry, "date");
        if (!cJSON_IsString(date) || !date->valuestring) {
            continue;
        }
        if (!applies_here(entry, subdivision)) {
            continue;
        }

        /* localName is what a regional calendar prints; name is the English
         * form. Prefer the local one, fall back if it is missing. */
        const cJSON *local = cJSON_GetObjectItemCaseSensitive(entry, "localName");
        const cJSON *english = cJSON_GetObjectItemCaseSensitive(entry, "name");
        const cJSON *chosen = cJSON_IsString(local) ? local : english;
        if (!cJSON_IsString(chosen) || !chosen->valuestring) {
            continue;
        }

        /* The same date can appear more than once (a regional and a nationwide
         * entry); keep the first. */
        bool dup = false;
        for (int i = 0; i < s_count; i++) {
            if (strcmp(s_items[i].date, date->valuestring) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }

        strlcpy(s_items[s_count].date, date->valuestring, sizeof(s_items[s_count].date));
        strlcpy(s_items[s_count].name, chosen->valuestring, sizeof(s_items[s_count].name));
        s_count++;
        added++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "%d holidays apply to %s", added, subdivision[0] ? subdivision : "(nationwide)");
    return ESP_OK;
}

esp_err_t holidays_ensure(const geo_location_t *loc, int year)
{
    ESP_RETURN_ON_FALSE(loc, ESP_ERR_INVALID_ARG, TAG, "bad args");

    if (year_loaded(year)) {
        return ESP_OK;
    }
    if (!loc->country_code[0]) {
        ESP_LOGW(TAG, "no country code, skipping holidays");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_year_count >= MAX_YEARS) {
        return ESP_ERR_NO_MEM;
    }

    char url[128];
    int n = snprintf(url, sizeof(url), NAGER_URL_FMT, year, loc->country_code);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof(url), ESP_ERR_INVALID_SIZE, TAG, "url too long");

    char *body = NULL;
    esp_err_t err = http_get_body(url, &body, NULL);
    if (err != ESP_OK) {
        /* Not fatal: weekends are still marked, holidays simply go unflagged. */
        ESP_LOGW(TAG, "could not fetch %d holidays: %s", year, esp_err_to_name(err));
        return err;
    }

    char subdivision[16] = "";
    if (loc->region_code[0]) {
        snprintf(subdivision, sizeof(subdivision), "%s-%s", loc->country_code, loc->region_code);
    }

    err = parse_and_store(body, subdivision);
    free(body);

    if (err == ESP_OK) {
        s_years[s_year_count++] = year;
    }
    return err;
}

const char *holidays_name(const char *iso_date)
{
    if (!iso_date) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_items[i].date, iso_date, 10) == 0) {
            return s_items[i].name;
        }
    }
    return NULL;
}

void holidays_clear(void)
{
    s_count = 0;
    s_year_count = 0;
}
