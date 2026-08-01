#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetch a URL and return the whole body as a NUL-terminated string.
 *
 * Handles both http:// and https:// (TLS verified against the bundled root CA
 * store). Redirects are followed by esp_http_client.
 *
 * @param url       Absolute URL
 * @param out_body  Receives a heap buffer the caller must free()
 * @param out_len   Optional, receives body length excluding the NUL
 */
esp_err_t http_get_body(const char *url, char **out_body, size_t *out_len);

#ifdef __cplusplus
}
#endif
