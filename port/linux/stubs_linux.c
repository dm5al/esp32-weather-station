/*
 * The remaining pieces main.c expects from the ESP32 board that have no
 * meaning here.
 */
#include <stddef.h>

#include "bsp/board.h"
#include "bsp/lcd_console.h"
#include "esp_log.h"

static const char *TAG = "stub";

/*
 * On the ESP32 this is a REPL on the serial port, used to tune panel timings
 * that do not exist on HDMI and to poll a touch controller that is not fitted.
 *
 * A Linux host already has a shell, which is a better console than anything
 * this could offer, so the command set is simply absent rather than
 * reimplemented. Said out loud at startup so nobody goes looking for it.
 */
esp_err_t lcd_console_start(void)
{
    ESP_LOGI(TAG, "no serial console on this target; use the shell");
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_handle(void)
{
    return NULL;
}

esp_err_t bsp_touch_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    (void)reg;
    (void)buf;
    (void)len;
    return ESP_ERR_NOT_SUPPORTED;
}
