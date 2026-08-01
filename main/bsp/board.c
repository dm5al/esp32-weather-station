#include "bsp/board.h"

#include <inttypes.h>

#include "bsp/ch422g.h"
#include "bsp/lcd_timing.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

/* ---------------------------------------------------------------------------
 * Waveshare ESP32-S3-Touch-LCD-7 pin map
 *
 * The panel is a raw 800x480 RGB565 TFT: the S3's LCD_CAM peripheral scans it
 * continuously out of PSRAM, so there is no command interface and no init
 * sequence — only timings.
 *
 * ESP-IDF orders data_gpio_nums as [0..4]=B, [5..10]=G, [11..15]=R, LSB first.
 * The panel's low colour bits are tied off on the PCB, hence the mapping below
 * starts at B3/G2/R3.
 * ------------------------------------------------------------------------- */
#define WS_LCD_HSYNC 46
#define WS_LCD_VSYNC 3
#define WS_LCD_DE    5
#define WS_LCD_PCLK  7

#define WS_LCD_B3 14
#define WS_LCD_B4 38
#define WS_LCD_B5 18
#define WS_LCD_B6 17
#define WS_LCD_B7 10

#define WS_LCD_G2 39
#define WS_LCD_G3 0
#define WS_LCD_G4 45
#define WS_LCD_G5 48
#define WS_LCD_G6 47
#define WS_LCD_G7 21

#define WS_LCD_R3 1
#define WS_LCD_R4 2
#define WS_LCD_R5 42
#define WS_LCD_R6 41
#define WS_LCD_R7 40

/* Shared I2C bus: GT911 touch + CH422G expander. */
#define WS_I2C_PORT I2C_NUM_0
#define WS_I2C_SDA  8
#define WS_I2C_SCL  9
#define WS_I2C_HZ   400000

#define WS_TP_INT 4

/*
 * CH422G outputs. The silkscreen "EXIO<n>" corresponds to the chip's IO<n>,
 * so EXIO1 is bit 1 — not bit 0. (EXIO0 is not brought out on this board.)
 */
#define WS_EXIO_TP_RST  CH422G_IO1
#define WS_EXIO_LCD_BL  CH422G_IO2 /* panel DISP pin: on/off only, no PWM */
#define WS_EXIO_LCD_RST CH422G_IO3
#define WS_EXIO_SD_CS   CH422G_IO4
#define WS_EXIO_USB_SEL CH422G_IO5

static lcd_timing_t s_timing;
static i2c_master_bus_handle_t s_i2c_bus;
static ch422g_handle_t s_expander;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_tp_io;
static esp_lcd_touch_handle_t s_touch;
static lv_display_t *s_disp;

static esp_err_t init_i2c_and_expander(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = WS_I2C_PORT,
        .sda_io_num = WS_I2C_SDA,
        .scl_io_num = WS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "i2c bus");
    ESP_RETURN_ON_ERROR(ch422g_create(s_i2c_bus, &s_expander), TAG, "ch422g");

    /*
     * Park the expander outputs in a safe state before anything else runs:
     * backlight off (so the user never sees an uninitialised frame buffer),
     * SD deselected, USB mux to the CH343 UART, panel + touch held in reset.
     */
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_LCD_BL, false), TAG, "bl");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_SD_CS, true), TAG, "sd_cs");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_USB_SEL, false), TAG, "usb_sel");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_LCD_RST, false), TAG, "lcd_rst");
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_TP_RST, false), TAG, "tp_rst");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_LCD_RST, true), TAG, "lcd_rst");
    return ESP_OK;
}

static esp_err_t init_lcd(void)
{
    /* Timings come from NVS when present so they can be tuned over the console
     * without a rebuild — see bsp/lcd_timing.h. */
    lcd_timing_load(&s_timing);
    ESP_LOGI(TAG, "timings: pclk=%" PRIu32 "MHz h(%u/%u/%u) v(%u/%u/%u) "
                  "hpol=%u vpol=%u depol=%u pclkneg=%u bb=%u lines%s",
             s_timing.pclk_mhz, s_timing.hpw, s_timing.hbp, s_timing.hfp, s_timing.vpw,
             s_timing.vbp, s_timing.vfp, s_timing.hsync_idle_low, s_timing.vsync_idle_low,
             s_timing.de_idle_high, s_timing.pclk_active_neg, s_timing.bb_lines,
             lcd_timing_is_overridden() ? " (from NVS)" : " (defaults)");

    if (s_timing.bb_lines && (BSP_LCD_V_RES % s_timing.bb_lines) != 0) {
        /* The driver derives its expected DMA-EOF count from fb_size/bb_size;
         * a remainder makes that count short and its underrun detector fires
         * on every frame, which looks like a trembling picture. */
        ESP_LOGW(TAG, "bb_lines=%u does not divide %d evenly — underrun detection "
                      "will misfire; use 8, 10, 12, 16, 20 or 24",
                 s_timing.bb_lines, BSP_LCD_V_RES);
    }

    const esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_width = 16,
        .bits_per_pixel = 16,
        /* Two PSRAM frame buffers: LVGL renders straight into them and we page
         * flip on vsync, which is what makes the UI tear-free. */
        .num_fbs = 2,
        /* Stage frames through internal SRAM. Reading PSRAM directly at the
         * pixel clock leaves no margin for bandwidth spikes, and an underrun
         * desyncs the scan permanently. */
        .bounce_buffer_size_px = s_timing.bb_lines * BSP_LCD_H_RES,
        .psram_trans_align = 64,
        .hsync_gpio_num = WS_LCD_HSYNC,
        .vsync_gpio_num = WS_LCD_VSYNC,
        .de_gpio_num = WS_LCD_DE,
        .pclk_gpio_num = WS_LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC, /* DISP hangs off the CH422G, not a GPIO */
        .data_gpio_nums = {
            WS_LCD_B3, WS_LCD_B4, WS_LCD_B5, WS_LCD_B6, WS_LCD_B7,
            WS_LCD_G2, WS_LCD_G3, WS_LCD_G4, WS_LCD_G5, WS_LCD_G6, WS_LCD_G7,
            WS_LCD_R3, WS_LCD_R4, WS_LCD_R5, WS_LCD_R6, WS_LCD_R7,
        },
        .timings = {
            .pclk_hz = s_timing.pclk_mhz * 1000 * 1000,
            .h_res = BSP_LCD_H_RES,
            .v_res = BSP_LCD_V_RES,
            .hsync_pulse_width = s_timing.hpw,
            .hsync_back_porch = s_timing.hbp,
            .hsync_front_porch = s_timing.hfp,
            .vsync_pulse_width = s_timing.vpw,
            .vsync_back_porch = s_timing.vbp,
            .vsync_front_porch = s_timing.vfp,
            .flags.hsync_idle_low = s_timing.hsync_idle_low,
            .flags.vsync_idle_low = s_timing.vsync_idle_low,
            .flags.de_idle_high = s_timing.de_idle_high,
            .flags.pclk_active_neg = s_timing.pclk_active_neg,
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, &s_panel), TAG, "new rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    return ESP_OK;
}

static esp_err_t init_touch(void)
{
    /*
     * The GT911 latches its I2C address from the INT pin on the rising edge of
     * reset: INT low -> 0x5D, INT high -> 0x14. Reset is on the expander rather
     * than a GPIO, so the driver cannot do this for us.
     */
    const gpio_config_t int_out = {
        .pin_bit_mask = 1ULL << WS_TP_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_out), TAG, "int gpio");
    ESP_RETURN_ON_ERROR(gpio_set_level(WS_TP_INT, 0), TAG, "int low");

    /* Datasheet sequence, and the hold times matter:
     *   both lines low >=100us, release RST >=5ms, then keep INT at the
     *   address-select level for >=50ms *after* RST rises — that is when the
     *   controller samples it and finishes booting. Releasing INT early leaves
     *   the chip answering on I2C but never reporting touches. */
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_TP_RST, false), TAG, "tp_rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(ch422g_set_level(s_expander, WS_EXIO_TP_RST, true), TAG, "tp_rst high");
    vTaskDelay(pdMS_TO_TICKS(55));

    /* Only now is the address latched; hand INT back to the GT911. */
    ESP_RETURN_ON_ERROR(gpio_reset_pin(WS_TP_INT), TAG, "int release");
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = WS_I2C_HZ;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c_v2(s_i2c_bus, &io_cfg, &s_tp_io), TAG, "tp io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        /* Both lines are driven through the expander / already handled above. */
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(s_tp_io, &tp_cfg, &s_touch), TAG, "gt911");
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_handle(void)
{
    return s_touch;
}

esp_err_t bsp_touch_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    ESP_RETURN_ON_FALSE(s_tp_io && buf, ESP_ERR_INVALID_STATE, TAG, "touch io not ready");
    return esp_lcd_panel_io_rx_param(s_tp_io, reg, buf, len);
}

static esp_err_t init_lvgl(void)
{
    const lvgl_port_cfg_t port_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = s_panel,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            /* avoid_tearing hands LVGL the panel's own frame buffers, so it
             * must render in direct mode to absolute coordinates. */
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            /* In bounce mode the "frame is out" signal comes from the bounce
             * finish callback, not vsync — esp_lvgl_port picks the right one
             * off this flag, so it has to track the panel config. */
            .bb_mode = s_timing.bb_lines > 0,
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "add rgb disp");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = s_touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "add touch");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    ESP_RETURN_ON_ERROR(init_i2c_and_expander(), TAG, "i2c/expander");
    ESP_RETURN_ON_ERROR(init_lcd(), TAG, "lcd");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "touch");
    ESP_RETURN_ON_ERROR(init_lvgl(), TAG, "lvgl");
    ESP_LOGI(TAG, "board ready: %dx%d RGB565, GT911 touch, LVGL running",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

esp_err_t bsp_display_backlight(bool on)
{
    ESP_RETURN_ON_FALSE(s_expander, ESP_ERR_INVALID_STATE, TAG, "expander not ready");
    return ch422g_set_level(s_expander, WS_EXIO_LCD_BL, on);
}
