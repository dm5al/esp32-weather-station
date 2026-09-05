/*
 * NVS has no flash partition to initialise here — the store is a directory of
 * files, created on first write. Both calls exist so main.c compiles unchanged.
 */
#pragma once

#include "esp_err.h"
#include "nvs.h"

static inline esp_err_t nvs_flash_init(void)
{
    return ESP_OK;
}

static inline esp_err_t nvs_flash_erase(void)
{
    return ESP_OK;
}
