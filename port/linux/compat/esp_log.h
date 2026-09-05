/*
 * ESP_LOGx onto stderr, with the same tag-and-level shape so log lines read the
 * same on both targets.
 *
 * Colour is applied only when stderr is a terminal: these lines end up in
 * journald when the port runs as a service, and escape sequences there are
 * noise in every future grep.
 */
#pragma once

#include <stdio.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0 none, 1 error, 2 warn, 3 info, 4 debug, 5 verbose. */
extern int esp_log_level;

const char *esp_log_colour(int level);
const char *esp_log_reset(void);

#define ESP_LOG_AT(lvl, letter, tag, fmt, ...)                                            \
    do {                                                                                  \
        if (esp_log_level >= (lvl)) {                                                     \
            fprintf(stderr, "%s" letter " (%s): " fmt "%s\n", esp_log_colour(lvl), (tag), \
                    ##__VA_ARGS__, esp_log_reset());                                      \
        }                                                                                 \
    } while (0)

#define ESP_LOGE(tag, fmt, ...) ESP_LOG_AT(1, "E", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_AT(2, "W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_AT(3, "I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ESP_LOG_AT(4, "D", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) ESP_LOG_AT(5, "V", tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
