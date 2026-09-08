/*
 * LVGL configuration for the Linux build.
 *
 * The firmware's LVGL settings come from Kconfig through esp_lvgl_port; this is
 * the same set expressed the ordinary way. Where the two must agree they do:
 * RGB565 colour depth, and the same font set, because the interface code names
 * fonts by symbol and would fail to link without them.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * Guarded because this file is also preprocessed by the assembler.
 *
 * LVGL ships hand-written .S files - Helium and NEON blenders - and every one of
 * them includes lv_conf_internal.h, which reaches this file, in order to test
 * whether it should assemble to anything at all. Its own convention is to define
 * __ASSEMBLY__ before doing so. Without this guard the C declarations in
 * stdint.h are handed to gas, which reports every typedef as a bad instruction:
 * hundreds of errors, none of them about the real problem.
 */
#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

/*
 * RGB565 to match the ESP32 build.
 *
 * A Pi could drive 32-bit colour and it would look marginally better, but the
 * weather icons are drawn from gradient-filled primitives tuned against 16-bit
 * output, and the Pi Zero W has one core to composite with. Matching the
 * firmware also means what is seen on one target is what is seen on the other.
 */
#define LV_COLOR_DEPTH 16

/*
 * No hand-written assembly in the blenders. Helium is Cortex-M and NEON needs
 * ARMv7; the Pi Zero W is an ARMv6 with VFP and neither applies. Stated rather
 * than left to the default so the .S files above assemble to nothing on purpose.
 */
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/* Milliseconds come from bsp_linux.c via lv_tick_set_cb(). */
#define LV_TICK_CUSTOM 0

#define LV_DRAW_BUF_ALIGN 4

/* ---- display backends ---------------------------------------------------- */
/* All three are compiled in; bsp_linux.c picks one with BSP_BACKEND_*. Building
 * the unused ones costs a few kilobytes and avoids a second place to edit when
 * the backend changes. */
#define LV_USE_LINUX_DRM   1
#define LV_USE_LINUX_FBDEV 1
#define LV_USE_SDL         1
#define LV_USE_EVDEV       1

#define LV_LINUX_FBDEV_BSD 0

/* ---- fonts --------------------------------------------------------------- */
/*
 * The interface uses its own Montserrat renderings from main/ui/fonts, which
 * are compiled in as C arrays, so LVGL's built-ins are almost all off. 14 stays
 * because LVGL uses it as the default for anything that does not set a font,
 * and turning it off makes such a widget draw nothing at all.
 */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* ---- what the interface actually uses ------------------------------------ */
#define LV_USE_LABEL     1
#define LV_USE_BUTTON    1
#define LV_USE_IMAGE     1
#define LV_USE_LINE      1
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_SLIDER    1
#define LV_USE_SWITCH    1
#define LV_USE_LIST      1
#define LV_USE_KEYBOARD  1
#define LV_USE_TEXTAREA  1
#define LV_USE_ROLLER    1
#define LV_USE_DROPDOWN  1
#define LV_USE_MSGBOX    1
#define LV_USE_SPINNER   1
#define LV_USE_ANIMIMG   1

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* ---- diagnostics --------------------------------------------------------- */
/*
 * Log level warning: LVGL's info chatter is not useful once the display works,
 * and on a Pi it goes to the journal where it would bury the app's own lines.
 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL       1
#define LV_USE_ASSERT_MALLOC     1
#define LV_USE_ASSERT_STYLE      0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ        0

/* Off: this is a fixed-purpose display, not a place to profile from. */
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR  0

#endif /* LV_CONF_H */
