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
