/*
 * Minimal CH422G I/O-expander driver built on the ESP-IDF v5 `i2c_master` API.
 *
 * Why not use espressif/esp32_io_expander? Its C port still talks to the
 * deprecated `driver/i2c.h` legacy driver. The GT911 touch panel on this board
 * shares the same I2C bus and esp_lcd_touch_gt911 is driven here through the
 * new i2c_master bus — ESP-IDF refuses to have both drivers bound to one port,
 * so the expander has to speak the new API too.
 *
 * CH422G is odd: it has no register pointer. Each "register" is a distinct I2C
 * device address that you simply write a byte to.
 *
 *   0x24  WR_SET  system config   bit0 IO_OE, bit1 A_SCAN, bit2 OD_EN, bit3 SLEEP
 *   0x23  WR_OC   open-drain outputs OC0..OC3 (low nibble)
 *   0x38  WR_IO   push-pull outputs IO0..IO7
 *   0x26  RD_IO   read IO0..IO7 (1-byte read, no register write first)
 *
 * On the Waveshare ESP32-S3-Touch-LCD-7 the silkscreen "EXIO<n>" maps to IO<n-1>.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Silkscreen EXIO number -> IO bit index. */
#define CH422G_IO0 0
#define CH422G_IO1 1
#define CH422G_IO2 2
#define CH422G_IO3 3
#define CH422G_IO4 4
#define CH422G_IO5 5
#define CH422G_IO6 6
#define CH422G_IO7 7

typedef struct ch422g_t *ch422g_handle_t;

/**
 * @brief Probe and initialise the CH422G on an existing i2c_master bus.
 *
 * Puts all eight IO pins into push-pull output mode and drives them low.
 *
 * @param bus  Bus handle from i2c_new_master_bus()
 * @param out  Receives the expander handle
 */
esp_err_t ch422g_create(i2c_master_bus_handle_t bus, ch422g_handle_t *out);

/** @brief Release the handle and its I2C device handles. */
esp_err_t ch422g_delete(ch422g_handle_t h);

/** @brief Drive a single output pin (CH422G_IOn). */
esp_err_t ch422g_set_level(ch422g_handle_t h, uint8_t io, bool level);

/** @brief Drive all eight outputs at once; bit n == IOn. */
esp_err_t ch422g_write_all(ch422g_handle_t h, uint8_t mask);

/** @brief Read the eight IO pins. Only meaningful for pins left as inputs. */
esp_err_t ch422g_read_all(ch422g_handle_t h, uint8_t *mask);

#ifdef __cplusplus
}
#endif
