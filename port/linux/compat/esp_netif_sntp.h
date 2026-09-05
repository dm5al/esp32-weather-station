/*
 * A no-op SNTP shim.
 *
 * On the ESP32 nothing else sets the clock, so the firmware has to run its own
 * SNTP client. A Raspberry Pi already has one — systemd-timesyncd or chrony —
 * and a second client fighting it for the same job is at best redundant.
 *
 * So this reports success without doing anything, which is honest: the clock
 * does get set, just not by us. The Pi Zero W has no battery-backed RTC, so
 * time is wrong until the network comes up; the display already handles that,
 * since the ESP32 has the same problem for the same reason.
 */
#pragma once

#include "esp_err.h"

typedef struct {
    const char *server;
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(host) ((esp_sntp_config_t){.server = (host)})

static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg)
{
    (void)cfg;
    return ESP_OK;
}
