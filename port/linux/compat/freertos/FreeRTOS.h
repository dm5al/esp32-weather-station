/*
 * Enough FreeRTOS to build main.c unmodified.
 *
 * The alternative was a second main_linux.c holding a copy of the state
 * machine: the refresh cadence, the retry behaviour, the order in which
 * geolocation, weather and holidays are fetched. Two copies of that would agree
 * on the day they were written and never again — a fix made in one would be
 * missing from the other, and the difference would only show up as a bug report
 * from whichever target nobody was testing.
 *
 * main.c is already written against a queue and a task, which map onto POSIX
 * closely enough that shimming the four calls it makes is smaller than the
 * duplication would have been.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t TickType_t;

#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdFAIL  0

/* One tick is one millisecond here, so the conversion is the identity. On the
 * ESP32 it depends on configTICK_RATE_HZ; nothing in this project relies on a
 * tick being any particular length. */
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portMAX_DELAY   ((TickType_t)0xFFFFFFFF)
#define portTICK_PERIOD_MS 1U

#define configMINIMAL_STACK_SIZE 2048
