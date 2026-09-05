#pragma once

#include <stdint.h>

/** @brief Microseconds since boot, from CLOCK_MONOTONIC. */
int64_t esp_timer_get_time(void);
