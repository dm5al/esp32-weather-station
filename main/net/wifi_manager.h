#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MGR_SSID_MAX 32
#define WIFI_MGR_PASS_MAX 64
#define WIFI_MGR_MAX_APS  20

typedef enum {
    WIFI_MGR_IDLE,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_FAILED, /* wrong password, AP gone, or retries exhausted */
} wifi_mgr_state_t;

typedef struct {
    char ssid[WIFI_MGR_SSID_MAX + 1];
    int8_t rssi;
    bool secure;
} wifi_mgr_ap_t;

/** @brief Called from the WiFi event task whenever the link state changes. */
typedef void (*wifi_mgr_state_cb_t)(wifi_mgr_state_t state, void *ctx);

/** @brief Init NVS-backed WiFi in station mode. Does not connect. */
esp_err_t wifi_mgr_init(wifi_mgr_state_cb_t cb, void *ctx);

/**
 * @brief Blocking active scan. Call from a worker task, never from the LVGL task.
 *
 * Results are sorted strongest-first and de-duplicated by SSID, since a mesh or
 * repeater will otherwise fill the picker with the same name several times.
 */
esp_err_t wifi_mgr_scan(wifi_mgr_ap_t *out, size_t max, size_t *out_found);

/**
 * @brief Associate with an AP. Returns as soon as the attempt starts; watch the
 *        state callback for the outcome.
 *
 * Credentials are only persisted once the connection actually succeeds, so a
 * typo can never overwrite a known-good network.
 */
esp_err_t wifi_mgr_connect(const char *ssid, const char *pass);

/** @brief True if a previously working network is stored in NVS. */
bool wifi_mgr_has_saved(void);

/** @brief Connect using the credentials stored by the last successful connect. */
esp_err_t wifi_mgr_connect_saved(void);

/** @brief Erase stored credentials and drop the current link. */
esp_err_t wifi_mgr_forget(void);

wifi_mgr_state_t wifi_mgr_get_state(void);

/** @brief SSID of the current/last attempted network, or "" if none. */
const char *wifi_mgr_current_ssid(void);

/** @brief RSSI of the current link in dBm, or 0 when not connected. */
int8_t wifi_mgr_rssi(void);

#ifdef __cplusplus
}
#endif
