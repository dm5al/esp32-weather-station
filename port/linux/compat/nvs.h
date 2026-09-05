/*
 * A file-backed stand-in for NVS.
 *
 * The portable sources store three things: the cached location, the chosen
 * language, and (on the ESP32) the Wi-Fi credentials. On Linux the first two
 * still need somewhere to live and the third is the operating system's problem,
 * so this implements exactly the handful of calls geolocate.c and i18n.c make.
 *
 * One namespace is one file under $XDG_CONFIG_HOME/esp32-weather (falling back
 * to ~/.config), holding "key=hex" lines. Hex rather than raw bytes because a
 * geo_location_t is a struct with padding in it, and a config file you can read
 * with cat is worth more here than a compact one.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_NVS_NOT_FOUND         0x1102
/* Named by main.c when it recovers a corrupt partition. Neither can happen to
 * a directory of files, so nothing here ever returns them. */
#define ESP_ERR_NVS_NO_FREE_PAGES     0x1110
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1117

typedef enum {
    NVS_READONLY,
    NVS_READWRITE,
} nvs_open_mode_t;

typedef struct nvs_store *nvs_handle_t;

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);

esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out);

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length);

esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value);
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *length);

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key);

/** @brief Where the store keeps its files. Created on first write. */
const char *nvs_store_dir(void);

#ifdef __cplusplus
}
#endif
