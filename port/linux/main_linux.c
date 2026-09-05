/*
 * Process entry for the Linux build.
 *
 * The application itself is main/main.c, unchanged and shared with the ESP32
 * firmware. All this does is what the ESP-IDF startup code would have done —
 * call app_main() — and then run LVGL's loop, which on the ESP32 belongs to
 * esp_lvgl_port.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "bsp/board.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "main";

void app_main(void); /* main/main.c */

void http_get_global_init(void);
void http_get_global_cleanup(void);

static volatile sig_atomic_t s_stop;

static void on_signal(int sig)
{
    (void)sig;
    s_stop = 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Ctrl-C and systemd's SIGTERM both leave the loop rather than killing the
     * process where it stands, so the display is released and the terminal is
     * not left in whatever state the framebuffer was in. */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    http_get_global_init();

    app_main();

    while (!s_stop) {
        bsp_display_lock(0);
        uint32_t next_ms = lv_timer_handler();
        bsp_display_unlock();

        /*
         * LVGL says when it next has work. Sleeping until then rather than
         * spinning is what keeps this at a few percent of one core on a Pi Zero
         * W, which has exactly one core and no headroom to waste on a display
         * that changes every fifteen minutes.
         */
        if (next_ms == LV_NO_TIMER_READY || next_ms > 100) {
            next_ms = 100;
        }
        struct timespec req = {
            .tv_sec = next_ms / 1000,
            .tv_nsec = (long)(next_ms % 1000) * 1000000L,
        };
        nanosleep(&req, NULL);
    }

    ESP_LOGI(TAG, "stopping");
    http_get_global_cleanup();
    return 0;
}
