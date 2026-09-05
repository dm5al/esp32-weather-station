#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "rtos";

/* ---- monotonic time ------------------------------------------------------ */

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    /* Monotonic, so the refresh cadence is not disturbed when NTP steps the
     * clock - which on a Pi with no RTC happens seconds after every boot. */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ---- tasks --------------------------------------------------------------- */

void vTaskDelay(TickType_t ticks)
{
    struct timespec req = {
        .tv_sec = ticks / 1000,
        .tv_nsec = (long)(ticks % 1000) * 1000000L,
    };
    /* Resume the remainder after a signal rather than returning early: callers
     * here treat a delay as "at least this long". */
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
    }
}

typedef struct {
    TaskFunction_t fn;
    void *arg;
} trampoline_t;

static void *trampoline(void *raw)
{
    trampoline_t *t = raw;
    TaskFunction_t fn = t->fn;
    void *arg = t->arg;
    free(t);
    fn(arg);
    return NULL;
}

int xTaskCreate(TaskFunction_t fn, const char *name, size_t stack_depth, void *arg, int priority,
                TaskHandle_t *out)
{
    (void)priority;

    trampoline_t *t = malloc(sizeof(*t));
    if (!t) {
        return pdFAIL;
    }
    t->fn = fn;
    t->arg = arg;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (stack_depth < (size_t)PTHREAD_STACK_MIN) {
        stack_depth = PTHREAD_STACK_MIN;
    }
    pthread_attr_setstacksize(&attr, stack_depth);

    pthread_t tid;
    int rc = pthread_create(&tid, &attr, trampoline, t);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        ESP_LOGE(TAG, "pthread_create(%s): %s", name ? name : "?", strerror(rc));
        free(t);
        return pdFAIL;
    }
#ifdef __linux__
    if (name) {
        /* Shows up in top and in a backtrace, same as the FreeRTOS task name. */
        pthread_setname_np(tid, name);
    }
#endif
    if (out) {
        *out = (TaskHandle_t)tid;
    }
    return pdPASS;
}

/* ---- queue --------------------------------------------------------------- */

struct queue_impl {
    uint8_t *buf;
    size_t item_size;
    size_t capacity;
    size_t count;
    size_t head;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

QueueHandle_t xQueueCreate(size_t length, size_t item_size)
{
    struct queue_impl *q = calloc(1, sizeof(*q));
    if (!q) {
        return NULL;
    }
    q->buf = calloc(length, item_size);
    if (!q->buf) {
        free(q);
        return NULL;
    }
    q->item_size = item_size;
    q->capacity = length;
    pthread_mutex_init(&q->lock, NULL);

    /* Both condition variables wait on CLOCK_MONOTONIC so a timed wait is not
     * cut short or stretched when the wall clock is stepped by NTP. */
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&q->not_empty, &cattr);
    pthread_cond_init(&q->not_full, &cattr);
    pthread_condattr_destroy(&cattr);
    return q;
}

void vQueueDelete(QueueHandle_t q)
{
    if (!q) {
        return;
    }
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buf);
    free(q);
}

static void deadline_from(TickType_t ticks, struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += ticks / 1000;
    ts->tv_nsec += (long)(ticks % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

int xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    if (!q || !item) {
        return pdFAIL;
    }
    pthread_mutex_lock(&q->lock);

    while (q->count == q->capacity) {
        if (ticks == 0) {
            pthread_mutex_unlock(&q->lock);
            return pdFAIL;
        }
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&q->not_full, &q->lock);
        } else {
            struct timespec ts;
            deadline_from(ticks, &ts);
            if (pthread_cond_timedwait(&q->not_full, &q->lock, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->lock);
                return pdFAIL;
            }
        }
    }

    size_t tail = (q->head + q->count) % q->capacity;
    memcpy(q->buf + tail * q->item_size, item, q->item_size);
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    if (!q || !item) {
        return pdFAIL;
    }
    pthread_mutex_lock(&q->lock);

    while (q->count == 0) {
        if (ticks == 0) {
            pthread_mutex_unlock(&q->lock);
            return pdFAIL;
        }
        if (ticks == portMAX_DELAY) {
            pthread_cond_wait(&q->not_empty, &q->lock);
        } else {
            struct timespec ts;
            deadline_from(ticks, &ts);
            if (pthread_cond_timedwait(&q->not_empty, &q->lock, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&q->lock);
                return pdFAIL;
            }
        }
    }

    memcpy(item, q->buf + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return pdTRUE;
}
