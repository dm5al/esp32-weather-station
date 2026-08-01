#include "bsp/ch422g.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ch422g";

/* Each "register" is its own I2C device address (see header). */
#define CH422G_ADDR_WR_SET 0x24
#define CH422G_ADDR_WR_OC  0x23
#define CH422G_ADDR_WR_IO  0x38
#define CH422G_ADDR_RD_IO  0x26

#define WR_SET_BIT_IO_OE (1U << 0) /* IO0..IO7 are push-pull outputs */

#define CH422G_I2C_HZ      400000
#define CH422G_TIMEOUT_MS  50

struct ch422g_t {
    i2c_master_dev_handle_t wr_set;
    i2c_master_dev_handle_t wr_oc;
    i2c_master_dev_handle_t wr_io;
    i2c_master_dev_handle_t rd_io;
    uint8_t io_state; /* shadow of WR_IO — the chip is write-only there */
};

static esp_err_t add_dev(i2c_master_bus_handle_t bus, uint16_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CH422G_I2C_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

esp_err_t ch422g_create(i2c_master_bus_handle_t bus, ch422g_handle_t *out)
{
    ESP_RETURN_ON_FALSE(bus && out, ESP_ERR_INVALID_ARG, TAG, "bad args");

    struct ch422g_t *h = calloc(1, sizeof(struct ch422g_t));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "calloc failed");

    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_ERROR(add_dev(bus, CH422G_ADDR_WR_SET, &h->wr_set), err, TAG, "add WR_SET");
    ESP_GOTO_ON_ERROR(add_dev(bus, CH422G_ADDR_WR_OC, &h->wr_oc), err, TAG, "add WR_OC");
    ESP_GOTO_ON_ERROR(add_dev(bus, CH422G_ADDR_WR_IO, &h->wr_io), err, TAG, "add WR_IO");
    ESP_GOTO_ON_ERROR(add_dev(bus, CH422G_ADDR_RD_IO, &h->rd_io), err, TAG, "add RD_IO");

    /* IO_OE = 1 -> IO0..IO7 become push-pull outputs. */
    uint8_t set = WR_SET_BIT_IO_OE;
    ESP_GOTO_ON_ERROR(i2c_master_transmit(h->wr_set, &set, 1, CH422G_TIMEOUT_MS), err, TAG,
                      "CH422G not responding at 0x%02x", CH422G_ADDR_WR_SET);

    h->io_state = 0x00;
    ESP_GOTO_ON_ERROR(i2c_master_transmit(h->wr_io, &h->io_state, 1, CH422G_TIMEOUT_MS), err, TAG,
                      "WR_IO failed");

    ESP_LOGI(TAG, "CH422G ready (IO0-IO7 push-pull, all low)");
    *out = h;
    return ESP_OK;

err:
    ch422g_delete(h);
    return ret;
}

esp_err_t ch422g_delete(ch422g_handle_t h)
{
    if (!h) {
        return ESP_OK;
    }
    if (h->wr_set) {
        i2c_master_bus_rm_device(h->wr_set);
    }
    if (h->wr_oc) {
        i2c_master_bus_rm_device(h->wr_oc);
    }
    if (h->wr_io) {
        i2c_master_bus_rm_device(h->wr_io);
    }
    if (h->rd_io) {
        i2c_master_bus_rm_device(h->rd_io);
    }
    free(h);
    return ESP_OK;
}

esp_err_t ch422g_set_level(ch422g_handle_t h, uint8_t io, bool level)
{
    ESP_RETURN_ON_FALSE(h && io < 8, ESP_ERR_INVALID_ARG, TAG, "bad args");

    uint8_t next = level ? (h->io_state | (uint8_t)(1U << io))
                         : (h->io_state & (uint8_t) ~(1U << io));
    ESP_RETURN_ON_ERROR(i2c_master_transmit(h->wr_io, &next, 1, CH422G_TIMEOUT_MS), TAG, "WR_IO");
    h->io_state = next;
    return ESP_OK;
}

esp_err_t ch422g_write_all(ch422g_handle_t h, uint8_t mask)
{
    ESP_RETURN_ON_FALSE(h, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(h->wr_io, &mask, 1, CH422G_TIMEOUT_MS), TAG, "WR_IO");
    h->io_state = mask;
    return ESP_OK;
}

esp_err_t ch422g_read_all(ch422g_handle_t h, uint8_t *mask)
{
    ESP_RETURN_ON_FALSE(h && mask, ESP_ERR_INVALID_ARG, TAG, "bad args");
    /* RD_IO is a bare 1-byte read from its own address — no register write. */
    return i2c_master_receive(h->rd_io, mask, 1, CH422G_TIMEOUT_MS);
}
