/*
 * wifi_manager.h against a Linux host, where the operating system owns the
 * radio and this program is only a tenant.
 *
 * State, SSID and signal are read from the kernel. Scanning and joining are
 * delegated to NetworkManager when nmcli is present, and reported as
 * unsupported when it is not, so the settings screen can say so honestly
 * instead of appearing to work.
 *
 * Nothing here goes through a shell. The SSID and password arrive from an
 * on-screen keyboard, which means they are attacker-controlled in any setting
 * where the panel is reachable - a network called `; rm -rf ~` is a perfectly
 * legal SSID. Passing an argv array to execvp() means the strings are never
 * parsed by anything, so there is no quoting to get right.
 */
#include "net/wifi_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "wifi";

static wifi_mgr_state_cb_t s_cb;
static void *s_cb_ctx;
static char s_iface[32];
static char s_ssid[WIFI_MGR_SSID_MAX + 1];
static int8_t s_rssi;

/* ---- running a helper without a shell ------------------------------------ */

/**
 * @brief Run argv[] and capture stdout.
 *
 * @return exit status, or -1 if the program could not be run at all.
 */
static int run_capture(char *const argv[], char *out, size_t out_sz)
{
    int fds[2];
    if (out && pipe(fds) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        if (out) {
            close(fds[0]);
            close(fds[1]);
        }
        return -1;
    }

    if (pid == 0) {
        if (out) {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[0]);
            close(fds[1]);
        }
        /* Errors from the helper are its own business; keep them off our log. */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127); /* only reached if exec failed */
    }

    size_t n = 0;
    if (out) {
        close(fds[1]);
        ssize_t r;
        while (n + 1 < out_sz && (r = read(fds[0], out + n, out_sz - n - 1)) > 0) {
            n += (size_t)r;
        }
        out[n] = '\0';
        close(fds[0]);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* @p name is a literal at every call site, never anything from the network. */
static bool have_tool(const char *name)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name);
    char *const argv[] = {"/bin/sh", "-c", cmd, NULL};
    return run_capture(argv, NULL, 0) == 0;
}

/* ---- reading the kernel's view ------------------------------------------- */

/** @brief First interface listed in /proc/net/wireless. */
static bool find_iface(void)
{
    if (s_iface[0]) {
        return true;
    }
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) {
        return false;
    }
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Two header lines, then one row per interface. */
        if (++lineno <= 2) {
            continue;
        }
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = '\0';
        char *name = line;
        while (*name == ' ') {
            name++;
        }
        /* Bounded explicitly: the line came from a file, and an interface
         * name longer than the field means it is not one. */
        snprintf(s_iface, sizeof(s_iface), "%.*s", (int)(sizeof(s_iface) - 1), name);
        break;
    }
    fclose(f);
    return s_iface[0] != '\0';
}

/** @brief Signal level in dBm from /proc/net/wireless. */
static void read_rssi(void)
{
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) {
        return;
    }
    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        if (++lineno <= 2) {
            continue;
        }
        char name[32];
        int status;
        float link, level;
        if (sscanf(line, " %31[^:]: %d %f %f", name, &status, &link, &level) == 4) {
            s_rssi = (int8_t)level;
            break;
        }
    }
    fclose(f);
}

static void read_ssid(void)
{
    if (!find_iface()) {
        s_ssid[0] = '\0';
        return;
    }
    char *const argv[] = {"iwgetid", s_iface, "-r", NULL};
    char buf[128] = {0};
    if (run_capture(argv, buf, sizeof(buf)) != 0) {
        s_ssid[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    /* An SSID is 32 bytes at most, so anything longer is not one; bound the
     * copy explicitly rather than relying on snprintf to trim it quietly. */
    snprintf(s_ssid, sizeof(s_ssid), "%.*s", (int)(sizeof(s_ssid) - 1), buf);
}

/* ---- the interface ------------------------------------------------------- */

esp_err_t wifi_mgr_init(wifi_mgr_state_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;

    if (!find_iface()) {
        ESP_LOGW(TAG, "no wireless interface in /proc/net/wireless; "
                      "a wired connection still works, the Wi-Fi screen will not");
    } else {
        ESP_LOGI(TAG, "using %s", s_iface);
    }
    read_ssid();
    read_rssi();

    if (s_cb) {
        s_cb(wifi_mgr_get_state(), s_cb_ctx);
    }
    return ESP_OK;
}

wifi_mgr_state_t wifi_mgr_get_state(void)
{
    read_ssid();
    /*
     * Associated to an access point is what this reports, which is not the same
     * as having a working route to the internet. The fetches find that out for
     * themselves and the screen shows their result, so claiming more here would
     * only produce a display that says "connected" beside stale numbers.
     */
    return s_ssid[0] ? WIFI_MGR_CONNECTED : WIFI_MGR_IDLE;
}

const char *wifi_mgr_current_ssid(void)
{
    return s_ssid;
}

int8_t wifi_mgr_rssi(void)
{
    read_rssi();
    return s_rssi;
}

esp_err_t wifi_mgr_scan(wifi_mgr_ap_t *out, size_t max, size_t *out_found)
{
    ESP_RETURN_ON_FALSE(out && out_found, ESP_ERR_INVALID_ARG, TAG, "bad args");
    *out_found = 0;

    if (!have_tool("nmcli")) {
        ESP_LOGW(TAG, "nmcli not installed; cannot scan from here");
        return ESP_ERR_NOT_SUPPORTED;
    }

    char *const argv[] = {"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "dev", "wifi", "list", NULL};
    char buf[8192];
    if (run_capture(argv, buf, sizeof(buf)) != 0) {
        return ESP_FAIL;
    }

    size_t n = 0;
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line && n < max;
         line = strtok_r(NULL, "\n", &save)) {
        /* -t gives colon-separated fields, escaping any literal colon as "\:". */
        char ssid[WIFI_MGR_SSID_MAX + 1] = {0};
        size_t si = 0;
        char *p = line;
        for (; *p && si < sizeof(ssid) - 1; p++) {
            if (*p == '\\' && p[1]) {
                ssid[si++] = *++p;
            } else if (*p == ':') {
                break;
            } else {
                ssid[si++] = *p;
            }
        }
        if (!ssid[0] || *p != ':') {
            continue;
        }
        int signal = atoi(p + 1);

        /* An empty SECURITY field means an open network. */
        const char *sec = strchr(p + 1, ':');
        bool secure = !(sec && sec[1] == '\0');

        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ssid);
        /* nmcli reports 0-100; the UI wants dBm, and this is the mapping
         * NetworkManager itself uses in reverse. */
        out[n].rssi = (int8_t)((signal / 2) - 100);
        out[n].secure = secure;
        n++;
    }

    *out_found = n;
    ESP_LOGI(TAG, "scan found %u networks", (unsigned)n);
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const char *ssid, const char *pass)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "no ssid");

    if (!have_tool("nmcli")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_cb) {
        s_cb(WIFI_MGR_CONNECTING, s_cb_ctx);
    }

    /*
     * argv, never a command string. These two values come from an on-screen
     * keyboard and a broadcast SSID, so both are outside our control.
     */
    int rc;
    if (pass && pass[0]) {
        char *const argv[] = {"nmcli",         "dev",         "wifi", "connect", (char *)ssid,
                              "password",      (char *)pass,  NULL};
        rc = run_capture(argv, NULL, 0);
    } else {
        char *const argv[] = {"nmcli", "dev", "wifi", "connect", (char *)ssid, NULL};
        rc = run_capture(argv, NULL, 0);
    }

    read_ssid();
    read_rssi();
    wifi_mgr_state_t st = wifi_mgr_get_state();
    if (s_cb) {
        s_cb(st, s_cb_ctx);
    }

    if (rc != 0) {
        ESP_LOGW(TAG, "nmcli connect failed (%d)", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
 * NetworkManager keeps the profiles, so "saved" means it has one and joining it
 * is its business, not ours. Nothing is stored by this program.
 */
bool wifi_mgr_has_saved(void)
{
    return find_iface();
}

esp_err_t wifi_mgr_connect_saved(void)
{
    /* The system autoconnects on boot; by the time this runs it has either
     * happened or the radio has nothing to join. */
    read_ssid();
    read_rssi();
    if (s_cb) {
        s_cb(wifi_mgr_get_state(), s_cb_ctx);
    }
    return s_ssid[0] ? ESP_OK : ESP_FAIL;
}

esp_err_t wifi_mgr_forget(void)
{
    /*
     * Deliberately not implemented. On the ESP32 this erases one credential
     * this program stored itself; here it would delete a NetworkManager profile
     * the owner may use for everything else on the machine, possibly the only
     * route by which they reach it.
     */
    ESP_LOGW(TAG, "forget is the host's business: use nmcli con delete");
    return ESP_ERR_NOT_SUPPORTED;
}
