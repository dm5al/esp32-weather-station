/*
 * Just enough of ESP-IDF's error type to build the portable half of this
 * project on Linux.
 *
 * The alternative was to invent a native error type and edit every net/ and ui/
 * source to use it. That would have forked the code: two versions of weather.c
 * differing only in the spelling of their return values, drifting apart every
 * time either was touched. A shim is a smaller thing to maintain than a fork.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int esp_err_t;

#define ESP_OK   0
#define ESP_FAIL -1

/* Values mirror ESP-IDF's so a log line means the same thing on both targets. */
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_ARG      0x102
#define ESP_ERR_INVALID_STATE    0x103
#define ESP_ERR_INVALID_SIZE     0x104
#define ESP_ERR_NOT_FOUND        0x105
#define ESP_ERR_TIMEOUT          0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_NOT_SUPPORTED    0x10a

const char *esp_err_to_name(esp_err_t err);

/*
 * Aborts, as ESP_ERROR_CHECK does. The ESP32 version panics and reboots; here
 * the process dies and, under systemd, is restarted. Both amount to "this
 * cannot be carried on from", which is the only reason the app uses it — on
 * bring-up failures with no sensible fallback.
 */
#define ESP_ERROR_CHECK(x)                                                                    \
    do {                                                                                      \
        esp_err_t err_rc_ = (x);                                                              \
        if (err_rc_ != ESP_OK) {                                                              \
            fprintf(stderr, "ESP_ERROR_CHECK failed: %s (0x%x) at %s:%d\n",                   \
                    esp_err_to_name(err_rc_), err_rc_, __FILE__, __LINE__);                   \
            abort();                                                                          \
        }                                                                                     \
    } while (0)
