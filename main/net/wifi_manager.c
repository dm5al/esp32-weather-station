#include "net/wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

#define NVS_NS        "wifi"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

/* Give up after this many association attempts and let the UI ask the user. */
#define MAX_RETRY 4

static wifi_mgr_state_cb_t s_cb;
static void *s_cb_ctx;
static wifi_mgr_state_t s_state = WIFI_MGR_IDLE;
static int s_retry;
static bool s_want_connect;      /* distinguishes a real drop from our own disconnect() */
static bool s_pending_save;      /* creds to persist once this attempt succeeds */
static char s_ssid[WIFI_MGR_SSID_MAX + 1];
static char s_pass[WIFI_MGR_PASS_MAX + 1];

static void set_state(wifi_mgr_state_t st)
{
    if (s_state == st) {
        return;
    }
    s_state = st;
    if (s_cb) {
        s_cb(st, s_cb_ctx);
    }
}

static esp_err_t save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    esp_err_t err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    /* On a fresh device the namespace does not exist yet. That is the normal
     * first-boot path, not an error, so it must not be logged as one. */
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len);
        /* A saved open network legitimately has no password entry. */
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            pass[0] = '\0';
            err = ESP_OK;
        }
    }
    nvs_close(h);
    return err;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_want_connect) {
            esp_wifi_connect();
        }
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        if (!s_want_connect) {
            set_state(WIFI_MGR_IDLE);
            return;
        }
        if (s_retry < MAX_RETRY) {
            s_retry++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d", d->reason, s_retry, MAX_RETRY);
            /* Reconnect inline — never sleep here, this is the shared event
             * loop task and stalling it delays every other IDF event. Attempts
             * are naturally paced by how long an association takes to fail. */
            esp_wifi_connect();
            set_state(WIFI_MGR_CONNECTING);
        } else {
            ESP_LOGE(TAG, "giving up on '%s' (reason %d)", s_ssid, d->reason);
            s_want_connect = false;
            set_state(WIFI_MGR_FAILED);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected to '%s', ip " IPSTR, s_ssid, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        if (s_pending_save) {
            /* Only now do we know the credentials are good. */
            if (save_creds(s_ssid, s_pass) == ESP_OK) {
                ESP_LOGI(TAG, "credentials saved");
            }
            s_pending_save = false;
        }
        set_state(WIFI_MGR_CONNECTED);
    }
}

esp_err_t wifi_mgr_init(wifi_mgr_state_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL),
                        TAG, "ip handler");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    ESP_LOGI(TAG, "station ready");
    return ESP_OK;
}

esp_err_t wifi_mgr_scan(wifi_mgr_ap_t *out, size_t max, size_t *out_found)
{
    ESP_RETURN_ON_FALSE(out && max && out_found, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *out_found = 0;

    const wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {.min = 100, .max = 300},
    };
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true), TAG, "scan");

    uint16_t n = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&n), TAG, "ap num");
    if (n == 0) {
        return ESP_OK;
    }
    if (n > WIFI_MGR_MAX_APS * 2) {
        n = WIFI_MGR_MAX_APS * 2;
    }

    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    ESP_RETURN_ON_FALSE(recs, ESP_ERR_NO_MEM, TAG, "calloc");
    esp_err_t err = esp_wifi_scan_get_ap_records(&n, recs);
    if (err != ESP_OK) {
        free(recs);
        return err;
    }

    /* esp_wifi already returns records sorted by RSSI descending, so keeping
     * the first sighting of each SSID keeps the strongest BSSID. */
    size_t count = 0;
    for (uint16_t i = 0; i < n && count < max; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(out[j].ssid, ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        strlcpy(out[count].ssid, ssid, sizeof(out[count].ssid));
        out[count].rssi = recs[i].rssi;
        out[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    free(recs);

    *out_found = count;
    ESP_LOGI(TAG, "scan found %u APs (%u unique)", (unsigned)n, (unsigned)count);
    return ESP_OK;
}

static esp_err_t start_connect(const char *ssid, const char *pass, bool save_on_success)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "empty ssid");

    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));
    s_pending_save = save_on_success;
    s_retry = 0;
    s_want_connect = true;

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config");

    /* Only drop an existing association — calling disconnect while idle would
     * raise a spurious DISCONNECTED event that the handler would count as a
     * failed retry. */
    if (s_state == WIFI_MGR_CONNECTED) {
        esp_wifi_disconnect();
    }
    set_state(WIFI_MGR_CONNECTING);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        s_want_connect = false;
        set_state(WIFI_MGR_FAILED);
        return err;
    }
    ESP_LOGI(TAG, "connecting to '%s'", s_ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *pass)
{
    return start_connect(ssid, pass, true);
}

bool wifi_mgr_has_saved(void)
{
    char ssid[WIFI_MGR_SSID_MAX + 1];
    char pass[WIFI_MGR_PASS_MAX + 1];
    return load_creds(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && ssid[0] != '\0';
}

esp_err_t wifi_mgr_connect_saved(void)
{
    char ssid[WIFI_MGR_SSID_MAX + 1] = {0};
    char pass[WIFI_MGR_PASS_MAX + 1] = {0};
    ESP_RETURN_ON_ERROR(load_creds(ssid, sizeof(ssid), pass, sizeof(pass)), TAG, "no saved creds");
    /* Already in NVS — no need to rewrite it on success. */
    return start_connect(ssid, pass, false);
}

esp_err_t wifi_mgr_forget(void)
{
    s_want_connect = false;
    esp_wifi_disconnect();

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);

    s_ssid[0] = '\0';
    set_state(WIFI_MGR_IDLE);
    return err;
}

wifi_mgr_state_t wifi_mgr_get_state(void)
{
    return s_state;
}

const char *wifi_mgr_current_ssid(void)
{
    return s_ssid;
}

int8_t wifi_mgr_rssi(void)
{
    if (s_state != WIFI_MGR_CONNECTED) {
        return 0;
    }
    wifi_ap_record_t rec;
    if (esp_wifi_sta_get_ap_info(&rec) != ESP_OK) {
        return 0;
    }
    return rec.rssi;
}
