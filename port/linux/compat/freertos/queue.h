/*
 * A blocking bounded queue with the FreeRTOS spelling, over pthreads.
 *
 * Only the four calls main.c makes are here. Notably absent is any notion of
 * priority or ISR safety: nothing on this target posts from an interrupt, and a
 * shim that pretended otherwise would be inviting the mistake.
 */
#pragma once

#include <stddef.h>

#include "FreeRTOS.h"

typedef struct queue_impl *QueueHandle_t;

QueueHandle_t xQueueCreate(size_t length, size_t item_size);
void vQueueDelete(QueueHandle_t q);

/** @brief Copy an item in, waiting up to @p ticks for room. */
int xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);

/** @brief Copy an item out, waiting up to @p ticks for one to arrive. */
int xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
