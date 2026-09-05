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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

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
     * Optional by design. An HDMI monitor usually has no touch panel, and the
     * settings and Wi-Fi screens are the only places input is needed - the
     * weather screen is a display, not a control surface. If no device is
     * configured the program runs perfectly well with no pointer at all.
     */
    const char *evdev = getenv("LV_EVDEV");
    if (evdev && evdev[0]) {
        lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, evdev);
        if (indev) {
            lv_indev_set_display(indev, s_disp);
            ESP_LOGI(TAG, "input from %s", evdev);
        } else {
            ESP_LOGW(TAG, "cannot open %s; running without input", evdev);
        }
    } else {
        ESP_LOGI(TAG, "no LV_EVDEV set; running without input");
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
