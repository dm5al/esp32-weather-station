#include "bsp/lcd_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "bsp/lcd_timing.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui/ui.h"

static const char *TAG = "lcd_console";

/* Every settable field, so `set` needs no per-field branching. */
typedef enum { F_U32, F_U16, F_U8 } field_width_t;

typedef struct {
    const char *name;
    size_t offset;
    field_width_t width;
    uint32_t min;
    uint32_t max;
    const char *help;
} field_t;

#define FIELD(name_, member_, width_, min_, max_, help_)                                           \
    {name_, offsetof(lcd_timing_t, member_), width_, min_, max_, help_}

static const field_t k_fields[] = {
    FIELD("pclk", pclk_mhz, F_U32, 1, 40, "pixel clock in MHz"),
    FIELD("hpw", hpw, F_U16, 1, 255, "HSYNC pulse width"),
    FIELD("hbp", hbp, F_U16, 0, 255, "H back porch - moves picture RIGHT"),
    FIELD("hfp", hfp, F_U16, 0, 255, "H front porch"),
    FIELD("vpw", vpw, F_U16, 1, 255, "VSYNC pulse width"),
    FIELD("vbp", vbp, F_U16, 0, 255, "V back porch - moves picture DOWN"),
    FIELD("vfp", vfp, F_U16, 0, 255, "V front porch"),
    FIELD("bblines", bb_lines, F_U16, 0, 48, "bounce buffer height (0=off, must divide 480)"),
    FIELD("hpol", hsync_idle_low, F_U8, 0, 1, "HSYNC idles low"),
    FIELD("vpol", vsync_idle_low, F_U8, 0, 1, "VSYNC idles low"),
    FIELD("depol", de_idle_high, F_U8, 0, 1, "DE idles high"),
    FIELD("pclkneg", pclk_active_neg, F_U8, 0, 1, "latch on falling PCLK edge"),
};

#define FIELD_COUNT (sizeof(k_fields) / sizeof(k_fields[0]))

static uint32_t field_get(const lcd_timing_t *t, const field_t *f)
{
    const void *p = (const uint8_t *)t + f->offset;
    switch (f->width) {
    case F_U32: return *(const uint32_t *)p;
    case F_U16: return *(const uint16_t *)p;
    default:    return *(const uint8_t *)p;
    }
}

static void field_set(lcd_timing_t *t, const field_t *f, uint32_t v)
{
    void *p = (uint8_t *)t + f->offset;
    switch (f->width) {
    case F_U32: *(uint32_t *)p = v; break;
    case F_U16: *(uint16_t *)p = (uint16_t)v; break;
    default:    *(uint8_t *)p = (uint8_t)v; break;
    }
}

static const field_t *field_find(const char *name)
{
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        if (strcmp(k_fields[i].name, name) == 0) {
            return &k_fields[i];
        }
    }
    return NULL;
}

static void print_timings(void)
{
    lcd_timing_t t;
    lcd_timing_load(&t);

    printf("\nPanel timings (%s)\n", lcd_timing_is_overridden() ? "from NVS" : "compiled defaults");
    for (size_t i = 0; i < FIELD_COUNT; i++) {
        printf("  %-8s %5" PRIu32 "   %s\n", k_fields[i].name, field_get(&t, &k_fields[i]),
               k_fields[i].help);
    }

    /* Total line/frame time is what the panel actually has to agree with. */
    uint32_t htotal = t.hpw + t.hbp + BSP_LCD_H_RES + t.hfp;
    uint32_t vtotal = t.vpw + t.vbp + BSP_LCD_V_RES + t.vfp;
    uint32_t refresh = (t.pclk_mhz * 1000000UL) / (htotal * vtotal);
    printf("  htotal %" PRIu32 ", vtotal %" PRIu32 ", refresh ~%" PRIu32 " Hz\n\n", htotal, vtotal,
           refresh);
}

static int cmd_lcd(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "show") == 0) {
        print_timings();
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        lcd_timing_clear();
        printf("Cleared. Rebooting on compiled defaults...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }

    if (strcmp(argv[1], "grid") == 0) {
        bool on = (argc >= 3) && strcmp(argv[2], "on") == 0;
        if (bsp_display_lock(1000)) {
            ui_set_alignment_grid(on);
            bsp_display_unlock();
            printf("Alignment grid %s\n", on ? "on" : "off");
        } else {
            printf("Could not take the display lock\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 4 || ((argc - 2) % 2) != 0) {
            printf("Usage: lcd set <field> <value> [<field> <value> ...]\n");
            return 1;
        }

        lcd_timing_t t;
        lcd_timing_load(&t);

        /* Validate everything before writing anything — a half-applied set that
         * then reboots into a bad mode is a miserable thing to debug. */
        for (int i = 2; i < argc; i += 2) {
            const field_t *f = field_find(argv[i]);
            if (!f) {
                printf("Unknown field '%s'. Try: lcd show\n", argv[i]);
                return 1;
            }
            char *end = NULL;
            unsigned long v = strtoul(argv[i + 1], &end, 0);
            if (!end || *end != '\0') {
                printf("'%s' is not a number\n", argv[i + 1]);
                return 1;
            }
            if (v < f->min || v > f->max) {
                printf("%s must be %" PRIu32 "..%" PRIu32 "\n", f->name, f->min, f->max);
                return 1;
            }
            field_set(&t, f, (uint32_t)v);
        }

        /* Guard the one constraint the min/max range cannot express. */
        if (t.bb_lines && (BSP_LCD_V_RES % t.bb_lines) != 0) {
            printf("bblines must divide %d evenly (try 8, 10, 12, 16, 20, 24)\n", BSP_LCD_V_RES);
            return 1;
        }

        esp_err_t err = lcd_timing_save(&t);
        if (err != ESP_OK) {
            printf("Save failed: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("Saved. Rebooting to apply...\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }

    printf("Usage:\n"
           "  lcd [show]                  show timings in use\n"
           "  lcd set <field> <val> ...   change, save and reboot\n"
           "  lcd reset                   forget override, reboot on defaults\n"
           "  lcd grid on|off             alignment overlay\n");
    return 0;
}

/* GT911 register map, the few that matter for diagnosis. */
#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_STATUS     0x814E

static int cmd_touch(int argc, char **argv)
{
    esp_lcd_touch_handle_t tp = bsp_touch_handle();
    if (!tp) {
        printf("Touch not initialised\n");
        return 1;
    }

    uint8_t id[4] = {0};
    if (bsp_touch_read_reg(GT911_REG_PRODUCT_ID, id, sizeof(id)) == ESP_OK) {
        printf("GT911 product id: '%c%c%c' cfg ver %u\n", id[0], id[1], id[2], id[3]);
    } else {
        printf("GT911 does not answer on I2C — check wiring/address\n");
        return 1;
    }

    int seconds = (argc >= 2) ? atoi(argv[1]) : 6;
    if (seconds < 1 || seconds > 60) {
        seconds = 6;
    }
    printf("Touch the panel now — polling for %d s\n", seconds);
    printf("  status = GT911 register 0x814E: bit7 = data ready, low nibble = point count\n\n");

    int samples = seconds * 20;
    int reported = 0;
    uint8_t last_status = 0xFF;

    for (int i = 0; i < samples; i++) {
        uint8_t status = 0;
        bsp_touch_read_reg(GT911_REG_STATUS, &status, 1);

        /* Same calls esp_lvgl_port makes, so this reflects what LVGL sees. */
        esp_lcd_touch_read_data(tp);
        uint16_t x[1] = {0};
        uint16_t y[1] = {0};
        uint8_t cnt = 0;
        bool pressed = esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 1);

        if (pressed && cnt > 0) {
            printf("  touch: x=%4u y=%4u  (raw status 0x%02x)\n", x[0], y[0], status);
            reported++;
        } else if (status != last_status && (status & 0x0F)) {
            /* Chip says it has points but the driver handed us nothing. */
            printf("  status 0x%02x reports %u point(s), driver returned none\n", status,
                   status & 0x0F);
        }
        last_status = status;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    printf("\n%d touch reports in %d s.\n", reported, seconds);
    if (reported == 0) {
        printf("Nothing registered. If 'status' stayed 0x00 the controller never saw a\n"
               "touch; if it showed points, the problem is downstream of the driver.\n");
    }
    return 0;
}

esp_err_t lcd_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "weather>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console init failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_console_cmd_t cmd = {
        .command = "lcd",
        .help = "Show or tune the RGB panel timings",
        .hint = NULL,
        .func = cmd_lcd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    const esp_console_cmd_t touch_cmd = {
        .command = "touch",
        .help = "Poll the GT911 and report raw touch data",
        .hint = "[seconds]",
        .func = cmd_touch,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&touch_cmd));
    ESP_ERROR_CHECK(esp_console_register_help_command());

    ESP_LOGI(TAG, "console ready — type 'lcd' to see the panel timings");
    return esp_console_start_repl(repl);
}
