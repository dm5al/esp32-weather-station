/*
 * File-backed NVS. See nvs.h for why.
 *
 * Writes go through a temporary file and rename(2), which on the same
 * filesystem is atomic: a Pi that loses power mid-save keeps the previous
 * settings rather than a truncated file. This device is meant to be unplugged
 * without ceremony, so that matters more than it would on a server.
 */
#include "nvs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "nvs";

#define MAX_ENTRIES 32
#define MAX_KEY     32
#define MAX_VALUE   512 /* bytes, before hex expansion */

typedef struct {
    char key[MAX_KEY];
    uint8_t value[MAX_VALUE];
    size_t len;
} entry_t;

struct nvs_store {
    char path[512];
    entry_t entries[MAX_ENTRIES];
    int count;
    nvs_open_mode_t mode;
    bool dirty;
};

static char s_dir[400];

const char *nvs_store_dir(void)
{
    if (s_dir[0]) {
        return s_dir;
    }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(s_dir, sizeof(s_dir), "%s/esp32-weather", xdg);
    } else {
        const char *home = getenv("HOME");
        snprintf(s_dir, sizeof(s_dir), "%s/.config/esp32-weather", home ? home : ".");
    }
    return s_dir;
}

/* mkdir -p for the two levels this needs. */
static void ensure_dir(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void load(struct nvs_store *st)
{
    FILE *f = fopen(st->path, "r");
    if (!f) {
        return;
    }
    char line[MAX_VALUE * 2 + MAX_KEY + 8];
    while (fgets(line, sizeof(line), f) && st->count < MAX_ENTRIES) {
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        entry_t *e = &st->entries[st->count];
        /* Bounded explicitly: a key longer than the field is a corrupt file,
         * and truncating it deliberately beats relying on snprintf to do it. */
        snprintf(e->key, sizeof(e->key), "%.*s", (int)(sizeof(e->key) - 1), line);

        const char *hex = eq + 1;
        size_t n = 0;
        while (hex[0] && hex[1] && n < MAX_VALUE) {
            int hi = hex_val(hex[0]);
            int lo = hex_val(hex[1]);
            if (hi < 0 || lo < 0) {
                break;
            }
            e->value[n++] = (uint8_t)((hi << 4) | lo);
            hex += 2;
        }
        e->len = n;
        st->count++;
    }
    fclose(f);
}

static esp_err_t store(struct nvs_store *st)
{
    ensure_dir(nvs_store_dir());

    char tmp[540];
    snprintf(tmp, sizeof(tmp), "%s.tmp", st->path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot write %s: %s", tmp, strerror(errno));
        return ESP_FAIL;
    }
    for (int i = 0; i < st->count; i++) {
        fprintf(f, "%s=", st->entries[i].key);
        for (size_t k = 0; k < st->entries[i].len; k++) {
            fprintf(f, "%02x", st->entries[i].value[k]);
        }
        fputc('\n', f);
    }
    fflush(f);
    /* The rename below is only atomic with respect to what actually reached the
     * disk, so the data has to be down before the name moves. */
    fsync(fileno(f));
    fclose(f);

    if (rename(tmp, st->path) != 0) {
        ESP_LOGE(TAG, "cannot rename onto %s: %s", st->path, strerror(errno));
        unlink(tmp);
        return ESP_FAIL;
    }
    st->dirty = false;
    return ESP_OK;
}

static entry_t *find(struct nvs_store *st, const char *key)
{
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->entries[i].key, key) == 0) {
            return &st->entries[i];
        }
    }
    return NULL;
}

static esp_err_t put(nvs_handle_t h, const char *key, const void *value, size_t len)
{
    if (!h || !key || len > MAX_VALUE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (h->mode == NVS_READONLY) {
        return ESP_ERR_INVALID_STATE;
    }
    entry_t *e = find(h, key);
    if (!e) {
        if (h->count >= MAX_ENTRIES) {
            return ESP_ERR_NO_MEM;
        }
        e = &h->entries[h->count++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    memcpy(e->value, value, len);
    e->len = len;
    h->dirty = true;
    return ESP_OK;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t mode, nvs_handle_t *out)
{
    if (!namespace_name || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    struct nvs_store *st = calloc(1, sizeof(*st));
    if (!st) {
        return ESP_ERR_NO_MEM;
    }
    snprintf(st->path, sizeof(st->path), "%s/%s.conf", nvs_store_dir(), namespace_name);
    st->mode = mode;
    load(st);
    *out = st;
    return ESP_OK;
}

void nvs_close(nvs_handle_t h)
{
    if (!h) {
        return;
    }
    /* ESP-IDF discards uncommitted writes on close and so does this, so code
     * that forgets nvs_commit() misbehaves identically on both targets. */
    free(h);
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    return h->dirty ? store(h) : ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    return put(h, key, &value, 1);
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    if (!h || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    entry_t *e = find(h, key);
    if (!e || e->len != 1) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out = e->value[0];
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    return put(h, key, value, length);
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    if (!h || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    entry_t *e = find(h, key);
    if (!e) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!out) {
        *length = e->len;
        return ESP_OK;
    }
    if (*length < e->len) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, e->value, e->len);
    *length = e->len;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    return put(h, key, value, strlen(value) + 1);
}

esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *length)
{
    return nvs_get_blob(h, key, out, length);
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < h->count; i++) {
        if (strcmp(h->entries[i].key, key) == 0) {
            h->entries[i] = h->entries[--h->count];
            h->dirty = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
