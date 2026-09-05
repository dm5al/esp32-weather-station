/*
 * http_get_body() over libcurl.
 *
 * Same contract as the ESP-IDF implementation in main/net/http_get.c: fetch the
 * whole body, hand back a NUL-terminated heap buffer the caller frees. Every
 * source this project talks to answers in a few kilobytes of JSON, so reading
 * it whole is simpler than streaming and costs nothing worth measuring.
 *
 * TLS verification is left at libcurl's default, which means the system CA
 * store. On the ESP32 the root bundle has to be compiled in; on a Pi it is the
 * distribution's job, and quietly disabling verification to avoid thinking
 * about it would be worse than either.
 */
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "esp_check.h"
#include "esp_log.h"
#include "net/http_get.h"

static const char *TAG = "http";

/* A body far larger than any of these APIs returns is a sign something is
 * wrong - a captive portal, an error page - and not worth buffering. */
#define MAX_BODY (256 * 1024)

#define TIMEOUT_S 20L

typedef struct {
    char *buf;
    size_t len;
} sink_t;

static size_t on_data(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    sink_t *s = (sink_t *)userdata;
    size_t chunk = size * nmemb;

    if (s->len + chunk > MAX_BODY) {
        ESP_LOGW(TAG, "body over %d bytes, giving up", MAX_BODY);
        return 0; /* aborts the transfer */
    }
    char *grown = realloc(s->buf, s->len + chunk + 1);
    if (!grown) {
        return 0;
    }
    s->buf = grown;
    memcpy(s->buf + s->len, ptr, chunk);
    s->len += chunk;
    s->buf[s->len] = '\0';
    return chunk;
}

esp_err_t http_get_body(const char *url, char **out_body, size_t *out_len)
{
    ESP_RETURN_ON_FALSE(url && out_body, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *out_body = NULL;

    CURL *curl = curl_easy_init();
    ESP_RETURN_ON_FALSE(curl, ESP_FAIL, TAG, "curl_easy_init failed");

    sink_t sink = {0};
    esp_err_t err = ESP_FAIL;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "esp32-weather/1.0 (+Raspberry Pi port)");
    /* Without this a failed DNS lookup inside a signal-handling process can
     * take the whole program down; libcurl documents it as the safe default
     * for anything multi-threaded, and LVGL runs its own timers here. */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        ESP_LOGW(TAG, "%s: %s", url, curl_easy_strerror(rc));
        err = (rc == CURLE_OPERATION_TIMEDOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
        goto done;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "%s: HTTP %ld", url, status);
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    if (!sink.buf) {
        ESP_LOGW(TAG, "%s: empty body", url);
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }

    *out_body = sink.buf;
    sink.buf = NULL; /* ownership passes to the caller */
    if (out_len) {
        *out_len = sink.len;
    }
    err = ESP_OK;

done:
    free(sink.buf);
    curl_easy_cleanup(curl);
    return err;
}

void http_get_global_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_get_global_cleanup(void)
{
    curl_global_cleanup();
}
