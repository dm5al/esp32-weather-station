/*
 * The GT911 touch controller does not exist on this target, but board.h names
 * its handle type in a prototype and every file that includes board.h would
 * otherwise fail to compile.
 *
 * An opaque pointer is enough. bsp_touch_handle() returns NULL here and the
 * only caller — the serial console's touch diagnostic — is not built for Linux.
 */
#pragma once

typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;
