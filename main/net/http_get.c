#include "net/http_get.h"

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http";

/* Both APIs return a few KB; anything larger means something went wrong. */
#define HTTP_MAX_BODY (64 * 1024)

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool overflow;
} body_accum_t;

static esp_err_t accumulate(body_accum_t *a, const char *data, size_t len)
{
    if (a->overflow) {
        return ESP_OK;
    }
    if (a->len + len + 1 > HTTP_MAX_BODY) {
        ESP_LOGW(TAG, "response exceeds %d bytes, truncating", HTTP_MAX_BODY);
        a->overflow = true;
        return ESP_OK;
    }
    if (a->len + len + 1 > a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 2048;
        while (cap < a->len + len + 1) {
            cap *= 2;
        }
        char *p = realloc(a->buf, cap);
        if (!p) {
            a->overflow = true;
            return ESP_ERR_NO_MEM;
        }
        a->buf = p;
        a->cap = cap;
    }
    memcpy(a->buf + a->len, data, len);
    a->len += len;
    a->buf[a->len] = '\0';
    return ESP_OK;
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    body_accum_t *a = evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        /* Chunked or not, this callback gives us every byte of the body. */
        return accumulate(a, (const char *)evt->data, evt->data_len);
    case HTTP_EVENT_ERROR:
        ESP_LOGW(TAG, "transport error");
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t http_get_body(const char *url, char **out_body, size_t *out_len)
{
    if (!url || !out_body) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;

    body_accum_t accum = {0};

    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_http_event,
        .user_data = &accum,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(accum.buf);
        return ESP_FAIL;
    }
    /* ipapi.co rejects requests without a User-Agent. */
    esp_http_client_set_header(client, "User-Agent", "esp32-weather/1.0");
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
        free(accum.buf);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "GET %s -> HTTP %d", url, status);
        free(accum.buf);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (accum.overflow || !accum.buf) {
        free(accum.buf);
        return ESP_ERR_INVALID_SIZE;
    }

    *out_body = accum.buf;
    if (out_len) {
        *out_len = accum.len;
    }
    return ESP_OK;
}
