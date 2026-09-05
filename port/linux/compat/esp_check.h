/*
 * The two ESP_RETURN_ON_* macros the portable sources actually use.
 *
 * Deliberately not the whole family: only what net/ and ui/ reference is
 * defined here, so anything new they start using fails to compile rather than
 * silently doing something subtly different from the ESP-IDF version.
 */
#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ESP_RETURN_ON_ERROR(x, tag, fmt, ...)                       \
    do {                                                            \
        esp_err_t err_rc_ = (x);                                    \
        if (err_rc_ != ESP_OK) {                                    \
            ESP_LOGE(tag, fmt " (0x%x)", ##__VA_ARGS__, err_rc_);   \
            return err_rc_;                                         \
        }                                                           \
    } while (0)

#define ESP_RETURN_ON_FALSE(a, err_code, tag, fmt, ...) \
    do {                                                \
        if (!(a)) {                                     \
            ESP_LOGE(tag, fmt, ##__VA_ARGS__);          \
            return err_code;                            \
        }                                               \
    } while (0)

#define ESP_GOTO_ON_FALSE(a, err_code, goto_tag, tag, fmt, ...) \
    do {                                                        \
        if (!(a)) {                                             \
            ESP_LOGE(tag, fmt, ##__VA_ARGS__);                  \
            err = err_code;                                     \
            goto goto_tag;                                      \
        }                                                       \
    } while (0)
