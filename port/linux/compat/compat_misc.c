#include <stdio.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"

int esp_log_level = 3; /* info */

/* Colour only for a terminal: these lines land in journald when the port runs
 * as a service, and escape sequences there are noise in every later grep. */
static int use_colour(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = isatty(fileno(stderr)) ? 1 : 0;
    }
    return cached;
}

const char *esp_log_colour(int level)
{
    if (!use_colour()) {
        return "";
    }
    switch (level) {
    case 1:
        return "\033[31m"; /* red */
    case 2:
        return "\033[33m"; /* yellow */
    case 3:
        return "\033[32m"; /* green */
    default:
        return "";
    }
}

const char *esp_log_reset(void)
{
    return use_colour() ? "\033[0m" : "";
}

const char *esp_err_to_name(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE:
        return "ESP_ERR_INVALID_RESPONSE";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}
