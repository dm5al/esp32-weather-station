/*
 * ESP32-S3 weather station — Waveshare ESP32-S3-Touch-LCD-7.
 *
 * Threading model:
 *   - LVGL runs on its own task inside esp_lvgl_port. It must never block, so
 *     button callbacks only enqueue a ui_cmd_t.
 *   - This file's app task owns everything slow: Wi-Fi scans, geolocation and
 *     the Open-Meteo fetch. It touches LVGL only under bsp_display_lock().
 */
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "bsp/lcd_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net/geolocate.h"
#include "net/holidays.h"
#include "net/weather.h"
#include "net/wifi_manager.h"
#include "nvs_flash.h"
#include "ui/i18n.h"
#include "ui/ui.h"

static const char *TAG = "app";

#define REFRESH_INTERVAL_MS (15 * 60 * 1000) /* Open-Meteo updates ~every 15 min */
#define RETRY_INTERVAL_MS   (2 * 60 * 1000)
#define EVENT_QUEUE_LEN     8

typedef enum {
    EVT_UI_CMD,
    EVT_WIFI_STATE,
} evt_kind_t;

typedef struct {
    evt_kind_t kind;
    union {
        ui_cmd_t cmd;
        wifi_mgr_state_t wifi_state;
    };
} app_evt_t;

static QueueHandle_t s_queue;
static geo_location_t s_location;
static bool s_have_location;
static weather_data_t s_weather;
static int64_t s_next_refresh_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---- Called from the LVGL task ------------------------------------------ */
void ui_post_cmd(const ui_cmd_t *cmd)
{
    if (!s_queue || !cmd) {
        return;
    }
    app_evt_t evt = {.kind = EVT_UI_CMD};
    evt.cmd = *cmd;
    if (xQueueSend(s_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full, dropping %d", cmd->type);
    }
}

/* ---- Called from the Wi-Fi event task ----------------------------------- */
static void on_wifi_state(wifi_mgr_state_t state, void *ctx)
{
    (void)ctx;
    app_evt_t evt = {.kind = EVT_WIFI_STATE};
    evt.wifi_state = state;
    xQueueSend(s_queue, &evt, 0);
}

/* ---- UI helpers: every one of these takes the LVGL lock ------------------ */
static void show_status(const char *title, const char *detail, bool busy)
{
    if (bsp_display_lock(0)) {
        ui_show_status(title, detail, busy);
        bsp_display_unlock();
    }
}

/**
 * @brief Report a failure without destroying a usable screen.
 *
 * Once a forecast is on display, a slightly stale one is more useful than an
 * error page — the fetch retries on its own anyway.
 */
static void report_failure(const char *title, const char *detail)
{
    bool have_screen;
    if (bsp_display_lock(0)) {
        have_screen = ui_weather_has_data();
        bsp_display_unlock();
    } else {
        have_screen = false;
    }

    if (have_screen) {
        ESP_LOGW(TAG, "%s: %s (keeping last reading on screen)", title, detail);
    } else {
        show_status(title, detail, false);
    }
}

/**
 * @brief Start SNTP the first time we get a link.
 *
 * The system clock stays on UTC. The header clock adds the UTC offset that
 * Open-Meteo reports for the location, which already accounts for the current
 * DST state — that avoids shipping a timezone database for one label.
 */
static void start_sntp_once(void)
{
    static bool started;
    if (started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        started = true;
        ESP_LOGI(TAG, "SNTP started");
    } else {
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
    }
}

static void push_link_state(void)
{
    if (bsp_display_lock(0)) {
        ui_weather_set_link(wifi_mgr_get_state(), wifi_mgr_rssi());
        bsp_display_unlock();
    }
}

static void do_scan(void)
{
    static wifi_mgr_ap_t aps[WIFI_MGR_MAX_APS];
    size_t found = 0;

    esp_err_t err = wifi_mgr_scan(aps, WIFI_MGR_MAX_APS, &found);
    if (bsp_display_lock(0)) {
        if (err == ESP_OK) {
            ui_wifi_set_aps(aps, found);
        } else {
            ui_wifi_set_status(T(STR_SCAN_FAILED), true);
        }
        bsp_display_unlock();
    }
}

/**
 * @brief Refresh location (once) and weather, then repaint.
 *
 * The location is resolved only the first time and then reused: this is a fixed
 * display on a desk, and every extra call burns the free tier's quota.
 */
static void do_refresh(void)
{
    if (wifi_mgr_get_state() != WIFI_MGR_CONNECTED) {
        ESP_LOGW(TAG, "refresh skipped, not connected");
        s_next_refresh_ms = now_ms() + RETRY_INTERVAL_MS;
        return;
    }

    if (bsp_display_lock(0)) {
        ui_weather_set_busy(true);
        bsp_display_unlock();
    }

    if (!s_have_location) {
        show_status(T(STR_LOCATING), T(STR_LOCATING_DETAIL), true);
        if (geo_detect(&s_location) == ESP_OK) {
            s_have_location = true;
        } else {
            ESP_LOGE(TAG, "geolocation failed");
            report_failure(T(STR_LOC_FAILED), T(STR_LOC_FAILED_MSG));
            s_next_refresh_ms = now_ms() + RETRY_INTERVAL_MS;
            if (bsp_display_lock(0)) {
                ui_weather_set_busy(false);
                bsp_display_unlock();
            }
            return;
        }
    }

    esp_err_t err = weather_fetch(&s_location, &s_weather);

    /*
     * Public holidays for whichever years the forecast window touches — near
     * New Year it straddles two. Best-effort: if the lookup fails the forecast
     * still renders, just without holidays flagged.
     */
    if (err == ESP_OK) {
        int last_year = 0;
        for (int i = 0; i < s_weather.day_count; i++) {
            int y = atoi(s_weather.days[i].date);
            if (y > 1970 && y != last_year) {
                holidays_ensure(&s_location, y);
                last_year = y;
            }
        }
    }

    if (bsp_display_lock(0)) {
        if (err == ESP_OK) {
            ui_weather_update(&s_location, &s_weather);
            ui_weather_set_link(wifi_mgr_get_state(), wifi_mgr_rssi());
            ui_show_weather();
        }
        ui_weather_set_busy(false);
        bsp_display_unlock();
    }

    if (err == ESP_OK) {
        s_next_refresh_ms = now_ms() + REFRESH_INTERVAL_MS;
    } else {
        ESP_LOGE(TAG, "weather fetch failed: %s", esp_err_to_name(err));
        s_next_refresh_ms = now_ms() + RETRY_INTERVAL_MS;
        report_failure(T(STR_WX_FAILED), T(STR_WX_FAILED_MSG));
    }
}

static void handle_wifi_state(wifi_mgr_state_t state)
{
    ESP_LOGI(TAG, "wifi state -> %d", state);
    push_link_state();

    switch (state) {
    case WIFI_MGR_CONNECTING:
        show_status(T(STR_CONNECTING), wifi_mgr_current_ssid(), true);
        break;

    case WIFI_MGR_CONNECTED:
        start_sntp_once();
        /* Refresh right away; do_refresh() sets the next deadline. */
        do_refresh();
        break;

    case WIFI_MGR_FAILED:
        if (bsp_display_lock(0)) {
            ui_show_wifi();
            ui_wifi_set_status(T(STR_CONNECT_FAILED), true);
            bsp_display_unlock();
        }
        do_scan();
        break;

    case WIFI_MGR_IDLE:
        break;
    }
}

static void handle_ui_cmd(const ui_cmd_t *cmd)
{
    switch (cmd->type) {
    case UI_CMD_WIFI_SCAN:
        do_scan();
        break;
    case UI_CMD_WIFI_CONNECT:
        if (wifi_mgr_connect(cmd->ssid, cmd->pass) != ESP_OK) {
            if (bsp_display_lock(0)) {
                ui_wifi_set_status(T(STR_CONNECT_BUSY), true);
                bsp_display_unlock();
            }
        }
        break;
    case UI_CMD_REFRESH:
        do_refresh();
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;

    if (wifi_mgr_has_saved()) {
        show_status(T(STR_CONNECTING), T(STR_JOINING_SAVED), true);
        if (wifi_mgr_connect_saved() != ESP_OK) {
            if (bsp_display_lock(0)) {
                ui_show_wifi();
                bsp_display_unlock();
            }
            do_scan();
        }
    } else {
        if (bsp_display_lock(0)) {
            ui_show_wifi();
            ui_wifi_set_status(T(STR_SCANNING), false);
            bsp_display_unlock();
        }
        do_scan();
    }

    s_next_refresh_ms = now_ms() + REFRESH_INTERVAL_MS;

    while (1) {
        int64_t wait_ms = s_next_refresh_ms - now_ms();
        if (wait_ms < 0) {
            wait_ms = 0;
        }
        if (wait_ms > REFRESH_INTERVAL_MS) {
            wait_ms = REFRESH_INTERVAL_MS;
        }

        app_evt_t evt;
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
            if (evt.kind == EVT_UI_CMD) {
                handle_ui_cmd(&evt.cmd);
            } else {
                handle_wifi_state(evt.wifi_state);
            }
        } else {
            /* Timed out: either the refresh is due or we are just idling. */
            if (now_ms() >= s_next_refresh_ms) {
                do_refresh();
            }
            push_link_state();
        }
    }
}

void app_main(void)
{
    /*
     * NVS first, and before bsp_board_init(): the panel timings are stored
     * there, and nvs_open() fails outright if the partition has not been
     * mounted yet — a saved LCD override would be silently ignored on boot.
     */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    i18n_init();

    ESP_ERROR_CHECK(bsp_board_init());

    /* Build the UI before switching the backlight on so the first thing the
     * panel ever shows is a finished frame, not uninitialised PSRAM. */
    bsp_display_lock(0);
    ui_init();
    ui_show_status(T(STR_APP_NAME), T(STR_STARTING), true);
    bsp_display_unlock();

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(bsp_display_backlight(true));

    s_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(app_evt_t));
    ESP_ERROR_CHECK(s_queue ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(wifi_mgr_init(on_wifi_state, NULL));

    xTaskCreate(app_task, "app", 8192, NULL, 5, NULL);

    /* Last, so its banner does not get buried under the bring-up logs. */
    lcd_console_start();
}
