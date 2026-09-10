#include "net/geolocate.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net/http_get.h"
#include "nvs.h"

static const char *TAG = "geo";

#define NVS_NS      "geo"
#define NVS_KEY_LOC "loc"

static void copy_str(char *dst, size_t dst_sz, const cJSON *node)
{
    if (cJSON_IsString(node) && node->valuestring) {
        strlcpy(dst, node->valuestring, dst_sz);
    }
}

/* ipapi.co: {"latitude":52.52,"longitude":13.4,"city":"Berlin",
 *            "region":"Berlin","country_name":"Germany","timezone":"Europe/Berlin"} */
static esp_err_t parse_ipapi_co(const char *body, geo_location_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;

    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "latitude");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "longitude");
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        out->latitude = (float)lat->valuedouble;
        out->longitude = (float)lon->valuedouble;
        copy_str(out->city, sizeof(out->city), cJSON_GetObjectItemCaseSensitive(root, "city"));
        copy_str(out->region, sizeof(out->region), cJSON_GetObjectItemCaseSensitive(root, "region"));
        copy_str(out->country, sizeof(out->country),
                 cJSON_GetObjectItemCaseSensitive(root, "country_name"));
        copy_str(out->timezone, sizeof(out->timezone),
                 cJSON_GetObjectItemCaseSensitive(root, "timezone"));
        copy_str(out->country_code, sizeof(out->country_code),
                 cJSON_GetObjectItemCaseSensitive(root, "country_code"));
        copy_str(out->region_code, sizeof(out->region_code),
                 cJSON_GetObjectItemCaseSensitive(root, "region_code"));
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

/* ip-api.com: {"status":"success","country":"Germany","regionName":"Berlin",
 *              "city":"Berlin","lat":52.52,"lon":13.4,"timezone":"Europe/Berlin"} */
static esp_err_t parse_ip_api_com(const char *body, geo_location_t *out)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;

    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0 &&
        cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        out->latitude = (float)lat->valuedouble;
        out->longitude = (float)lon->valuedouble;
        copy_str(out->city, sizeof(out->city), cJSON_GetObjectItemCaseSensitive(root, "city"));
        copy_str(out->region, sizeof(out->region),
                 cJSON_GetObjectItemCaseSensitive(root, "regionName"));
        copy_str(out->country, sizeof(out->country),
                 cJSON_GetObjectItemCaseSensitive(root, "country"));
        copy_str(out->timezone, sizeof(out->timezone),
                 cJSON_GetObjectItemCaseSensitive(root, "timezone"));
        copy_str(out->country_code, sizeof(out->country_code),
                 cJSON_GetObjectItemCaseSensitive(root, "countryCode"));
        /* ip-api's "region" is the subdivision *code*; "regionName" is the
         * human-readable one read above. */
        copy_str(out->region_code, sizeof(out->region_code),
                 cJSON_GetObjectItemCaseSensitive(root, "region"));
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

static void cache_store(const geo_location_t *loc)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (nvs_set_blob(h, NVS_KEY_LOC, loc, sizeof(*loc)) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

esp_err_t geo_load_cached(geo_location_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    nvs_handle_t h;
    /* No namespace yet simply means nothing has been cached — not an error. */
    esp_err_t open_err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (open_err != ESP_OK) {
        return open_err;
    }
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_LOC, out, &len);
    nvs_close(h);

    if (err == ESP_OK && len != sizeof(*out)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

typedef esp_err_t (*geo_parser_t)(const char *body, geo_location_t *out);

esp_err_t geo_detect(geo_location_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    static const struct {
        const char *url;
        geo_parser_t parse;
    } providers[] = {
        {"https://ipapi.co/json/", parse_ipapi_co},
        /* ip-api.com only serves TLS to paying customers, so this leg is
         * plaintext. It carries no credentials and only reveals our public IP
         * to a party that already sees it, but it stays second in the list. */
        {"http://ip-api.com/json/?fields=status,country,countryCode,region,regionName,city,lat,lon,timezone",
         parse_ip_api_com},
    };

    for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); i++) {
        char *body = NULL;
        esp_err_t err = http_get_body(providers[i].url, &body, NULL);
        if (err != ESP_OK) {
            continue;
        }

        geo_location_t loc = {0};
        err = providers[i].parse(body, &loc);
        free(body);

        if (err == ESP_OK) {
            *out = loc;
            cache_store(&loc);
            ESP_LOGI(TAG, "located: %s, %s (%.4f, %.4f) tz=%s region=%s-%s",
                     loc.city[0] ? loc.city : "?", loc.country, loc.latitude, loc.longitude,
                     loc.timezone, loc.country_code, loc.region_code);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "provider %d returned unusable data", (int)i);
    }

    ESP_LOGW(TAG, "all geolocation providers failed, trying cache");
    return geo_load_cached(out);
}

/* ---- manual location ------------------------------------------------------
 *
 * Stored beside the automatic cache in the same namespace, under its own keys,
 * so switching back to automatic does not lose the chosen place and switching
 * to manual does not discard the last detected one.
 */

#define NVS_KEY_MODE   "mode"
#define NVS_KEY_MANUAL "manual"

geo_mode_t geo_get_mode(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return GEO_MODE_AUTO;
    }
    uint8_t stored = GEO_MODE_AUTO;
    if (nvs_get_u8(h, NVS_KEY_MODE, &stored) != ESP_OK || stored > GEO_MODE_MANUAL) {
        stored = GEO_MODE_AUTO;
    }
    nvs_close(h);
    return (geo_mode_t)stored;
}

esp_err_t geo_set_mode(geo_mode_t mode)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, NVS_KEY_MODE, (uint8_t)mode);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "location mode: %s", mode == GEO_MODE_MANUAL ? "manual" : "automatic");
    return err;
}

esp_err_t geo_get_manual(geo_location_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    nvs_handle_t h;
    esp_err_t open_err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (open_err != ESP_OK) {
        return open_err;
    }
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_MANUAL, out, &len);
    nvs_close(h);

    if (err == ESP_OK && len != sizeof(*out)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t geo_set_manual(const geo_location_t *loc)
{
    ESP_RETURN_ON_FALSE(loc, ESP_ERR_INVALID_ARG, TAG, "bad args");

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY_MANUAL, loc, sizeof(*loc));
    if (err == ESP_OK) {
        err = nvs_set_u8(h, NVS_KEY_MODE, (uint8_t)GEO_MODE_MANUAL);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    ESP_LOGI(TAG, "manual location: %s, %s (%.4f, %.4f) tz=%s", loc->city, loc->country,
             loc->latitude, loc->longitude, loc->timezone);
    return err;
}

esp_err_t geo_resolve(geo_location_t *out)
{
    ESP_RETURN_ON_FALSE(out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    if (geo_get_mode() == GEO_MODE_MANUAL) {
        if (geo_get_manual(out) == ESP_OK) {
            ESP_LOGI(TAG, "using manual location: %s (%.4f, %.4f)", out->city, out->latitude,
                     out->longitude);
            return ESP_OK;
        }
        /* Set to manual but never given a place. Detecting is better than
         * refusing to show a forecast at all. */
        ESP_LOGW(TAG, "manual mode with no place set; detecting instead");
    }
    return geo_detect(out);
}

/* ---- search --------------------------------------------------------------- */

#define GEOCODE_URL_FMT                                        \
    "https://geocoding-api.open-meteo.com/v1/search"           \
    "?name=%s&count=%d&language=en&format=json"

/* Percent-encode everything that is not unreserved. Place names carry spaces,
 * accents and the occasional apostrophe, and all of them arrive here from an
 * on-screen keyboard. */
static void url_escape(const char *src, char *dst, size_t dst_sz)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (; *src && n + 4 < dst_sz; src++) {
        unsigned char c = (unsigned char)*src;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[n++] = (char)c;
        } else {
            dst[n++] = '%';
            dst[n++] = hex[c >> 4];
            dst[n++] = hex[c & 0x0f];
        }
    }
    dst[n] = '\0';
}

esp_err_t geo_search(const char *query, geo_location_t *out, size_t max, size_t *found)
{
    ESP_RETURN_ON_FALSE(query && out && found && max, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *found = 0;

    char escaped[192];
    url_escape(query, escaped, sizeof(escaped));

    char url[320];
    int n = snprintf(url, sizeof(url), GEOCODE_URL_FMT, escaped, (int)max);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof(url), ESP_ERR_INVALID_SIZE, TAG, "url too long");

    char *body = NULL;
    ESP_RETURN_ON_ERROR(http_get_body(url, &body, NULL), TAG, "search failed");

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* No "results" key at all is how this API says "nothing matched"; that is
     * an empty list, not a failure. */
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(results)) {
        cJSON_Delete(root);
        return ESP_OK;
    }

    size_t count = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, results) {
        if (count >= max) {
            break;
        }
        const cJSON *lat = cJSON_GetObjectItemCaseSensitive(item, "latitude");
        const cJSON *lon = cJSON_GetObjectItemCaseSensitive(item, "longitude");
        if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
            continue;
        }

        geo_location_t *g = &out[count];
        memset(g, 0, sizeof(*g));
        g->latitude = (float)lat->valuedouble;
        g->longitude = (float)lon->valuedouble;
        copy_str(g->city, sizeof(g->city), cJSON_GetObjectItemCaseSensitive(item, "name"));
        copy_str(g->region, sizeof(g->region), cJSON_GetObjectItemCaseSensitive(item, "admin1"));
        copy_str(g->country, sizeof(g->country), cJSON_GetObjectItemCaseSensitive(item, "country"));
        copy_str(g->timezone, sizeof(g->timezone),
                 cJSON_GetObjectItemCaseSensitive(item, "timezone"));
        copy_str(g->country_code, sizeof(g->country_code),
                 cJSON_GetObjectItemCaseSensitive(item, "country_code"));
        /* region_code is deliberately left empty: see geo_search() in the
         * header. The geocoder returns the region's name, not its ISO code. */
        count++;
    }

    cJSON_Delete(root);
    *found = count;
    ESP_LOGI(TAG, "search \"%s\": %u results", query, (unsigned)count);
    return ESP_OK;
}
