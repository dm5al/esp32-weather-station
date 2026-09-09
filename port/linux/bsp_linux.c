/*
 * The board support layer for a Raspberry Pi, where "the board" is whatever
 * HDMI is plugged into.
 *
 * Three backends, chosen at build time because they need different libraries
 * and different privileges, not because a running program could sensibly switch:
 *
 *   DRM    the current path on Raspberry Pi OS Bookworm, which uses the KMS
 *          driver. Needs no X server and no desktop.
 *   FBDEV  /dev/fb0. Simplest, works headless, deprecated but still present.
 *   SDL    a window on a desktop, for developing this on a laptop.
 *
 * The UI is laid out at a fixed 800x480 in absolute pixel positions, which is
 * what the panel this started on happens to be. On a monitor reporting anything
 * else the layout does not reflow - it cannot, there are hard-coded coordinates
 * throughout ui_weather.c - so the mode is checked at startup and a mismatch is
 * said out loud rather than left for the user to puzzle over. See the README for
 * how to pin the mode in config.txt.
 */
#include "bsp/board.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "bsp";

static lv_display_t *s_disp;
static pthread_mutex_t s_lvgl_lock;

/* ---- the LVGL lock ------------------------------------------------------- */

/*
 * LVGL is not thread-safe, and this program has two threads that want it: the
 * app task fetching over the network, and the main loop running lv_timer_handler.
 * Same contract as the ESP-IDF port, so callers in main.c and ui/ need no
 * changes.
 */
bool bsp_display_lock(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return pthread_mutex_lock(&s_lvgl_lock) == 0;
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_mutex_timedlock(&s_lvgl_lock, &ts) == 0;
}

void bsp_display_unlock(void)
{
    pthread_mutex_unlock(&s_lvgl_lock);
}

/* ---- LVGL's millisecond source ------------------------------------------- */

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* ---- finding the touchscreen --------------------------------------------- */

/*
 * Asked of the kernel rather than configured.
 *
 * The first version took a path from LV_EVDEV and did nothing without it, on
 * the reasoning that an HDMI monitor rarely has a touch panel. The panel this
 * runs on does, and requiring an environment variable to notice hardware that
 * is plugged in and announcing itself is the wrong default. Event numbers are
 * also assigned in probe order, so a path that is right today can be wrong
 * after a reboot, or after a USB device is added.
 *
 * A touchscreen is an input device that reports absolute X and Y. A mouse
 * reports relative movement and is excluded by the same test.
 */
#define BITS_PER_LONG_   (sizeof(long) * 8)
#define NBITS_(x)        ((((x) - 1) / BITS_PER_LONG_) + 1)
#define TEST_BIT_(b, a)  (((a)[(b) / BITS_PER_LONG_] >> ((b) % BITS_PER_LONG_)) & 1)

static bool is_touchscreen(const char *path, char *name, size_t name_sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    unsigned long ev[NBITS_(EV_MAX)] = {0};
    unsigned long abs[NBITS_(ABS_MAX)] = {0};
    bool ok = false;

    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) >= 0 && TEST_BIT_(EV_ABS, ev) &&
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) >= 0 && TEST_BIT_(ABS_X, abs) &&
        TEST_BIT_(ABS_Y, abs)) {
        ok = true;
        if (name && name_sz && ioctl(fd, EVIOCGNAME(name_sz), name) < 0) {
            name[0] = '\0';
        }
    }
    close(fd);
    return ok;
}

static bool find_touchscreen(char *out, size_t out_sz, char *name, size_t name_sz)
{
    DIR *d = opendir("/dev/input");
    if (!d) {
        return false;
    }
    bool found = false;
    const struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        if (is_touchscreen(path, name, name_sz)) {
            snprintf(out, out_sz, "%s", path);
            found = true;
        }
    }
    closedir(d);
    return found;
}

/* ---- bring-up ------------------------------------------------------------ */

esp_err_t bsp_board_init(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    /* Recursive: ui_ code calls back into helpers that also take the lock, and
     * on the ESP32 the LVGL port's mutex is recursive too. */
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lvgl_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    lv_init();
    lv_tick_set_cb(tick_ms);

#if defined(BSP_BACKEND_DRM)
    s_disp = lv_linux_drm_create();
    if (!s_disp) {
        ESP_LOGE(TAG, "no DRM device; is another compositor holding it?");
        return ESP_FAIL;
    }
    lv_linux_drm_set_file(s_disp, getenv("LV_DRM_CARD") ? getenv("LV_DRM_CARD") : "/dev/dri/card0",
                          -1);
#elif defined(BSP_BACKEND_FBDEV)
    s_disp = lv_linux_fbdev_create();
    if (!s_disp) {
        ESP_LOGE(TAG, "cannot create fbdev display");
        return ESP_FAIL;
    }
    lv_linux_fbdev_set_file(s_disp, getenv("LV_FBDEV") ? getenv("LV_FBDEV") : "/dev/fb0");
#elif defined(BSP_BACKEND_SDL)
    s_disp = lv_sdl_window_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (!s_disp) {
        ESP_LOGE(TAG, "cannot open an SDL window");
        return ESP_FAIL;
    }
#else
#error "define one of BSP_BACKEND_DRM, BSP_BACKEND_FBDEV or BSP_BACKEND_SDL"
#endif

    int32_t w = lv_display_get_horizontal_resolution(s_disp);
    int32_t h = lv_display_get_vertical_resolution(s_disp);
    ESP_LOGI(TAG, "display %" PRId32 "x%" PRId32, w, h);

    if (w != BSP_LCD_H_RES || h != BSP_LCD_V_RES) {
        /*
         * Not fatal, because a wrong-sized picture is still a picture and the
         * operator can see what happened. But it will look wrong: the layout is
         * absolute, so on a larger screen it sits in the top-left corner and on
         * a smaller one it is clipped.
         */
        ESP_LOGW(TAG, "the interface is drawn for %dx%d and will not reflow to %" PRId32
                      "x%" PRId32 " - see the README on pinning the HDMI mode",
                 BSP_LCD_H_RES, BSP_LCD_V_RES, w, h);
    }

#if defined(LV_USE_EVDEV) && LV_USE_EVDEV
    /*
     * LV_EVDEV overrides, for the case where the guess is wrong or there is
     * more than one candidate; otherwise the touchscreen is found by asking.
     */
    char found[300];
    char name[128] = {0};
    const char *evdev = getenv("LV_EVDEV");

    if (!(evdev && evdev[0]) && find_touchscreen(found, sizeof(found), name, sizeof(name))) {
        evdev = found;
        ESP_LOGI(TAG, "touchscreen: %s (%s)", name[0] ? name : "unnamed", found);
    }

    if (evdev && evdev[0]) {
        lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev);
        if (indev) {
            lv_indev_set_display(indev, s_disp);
            ESP_LOGI(TAG, "input from %s", evdev);
        } else {
            /* Almost always the permission: the device is root:input, so the
             * account running this has to be in the input group. */
            ESP_LOGW(TAG, "cannot open %s; running without input", evdev);
        }
    } else {
        ESP_LOGI(TAG, "no pointer device found; running without input");
    }
#endif

    return ESP_OK;
}

esp_err_t bsp_display_backlight(bool on)
{
    /*
     * There is no backlight line to drive: HDMI monitors have their own. Kept
     * so main.c's bring-up sequence is identical on both targets, and it does
     * do something useful - the app calls it once the first frame is composed,
     * which is exactly when the display should start showing it.
     */
    ESP_LOGI(TAG, "display %s", on ? "on" : "off");
    return ESP_OK;
}

lv_display_t *bsp_display(void)
{
    return s_disp;
}
