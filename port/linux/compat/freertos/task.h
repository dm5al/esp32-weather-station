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
 * @p stack_depth is honoured as a byte count, floored at PTHREAD_STACK_MIN.
 * FreeRTOS counts it in words, but nothing here passes a value where the
 * distinction matters and a too-large stack on Linux costs only address space.
 * @p priority is ignored: this is not a real-time system and pretending to
 * schedule like one would mislead whoever reads the call site next.
 */
int xTaskCreate(TaskFunction_t fn, const char *name, size_t stack_depth, void *arg, int priority,
                TaskHandle_t *out);
