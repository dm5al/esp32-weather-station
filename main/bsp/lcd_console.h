/*
 * Serial console for dialling in the panel timings.
 *
 *   lcd                          show the values in use
 *   lcd set hbp 40 hfp 20        change one or more fields, save, reboot
 *   lcd reset                    forget the override, reboot on defaults
 *   lcd grid on                  overlay an alignment pattern
 *
 * Fields: pclk hpw hbp hfp vpw vbp vfp hpol vpol depol pclkneg
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Start the REPL on the console UART. */
esp_err_t lcd_console_start(void);

#ifdef __cplusplus
}
#endif
