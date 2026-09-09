#pragma once

#include <stddef.h>

#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

/** @brief Sleep the calling thread. */
void vTaskDelay(TickType_t ticks);

/**
 * @brief Start @p fn on a detached pthread.
 *
 * @p stack_depth is treated as a floor in bytes and ignored when it is below
 * the platform default, because a size chosen for FreeRTOS is far too small for
 * glibc: the figure that is comfortable there is not enough for one TLS
 * handshake here. @p priority is ignored: this is not a real-time system, and
 * pretending to schedule like one would mislead whoever reads the call site.
 */
int xTaskCreate(TaskFunction_t fn, const char *name, size_t stack_depth, void *arg, int priority,
                TaskHandle_t *out);
