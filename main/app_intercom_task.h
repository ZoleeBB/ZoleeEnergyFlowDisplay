#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

/* A WithCaps önmaga törlése új belső RAM-os takarítófeladatot foglal, és
 * sikertelen foglalásnál abortál. A kész worker inkább felfüggeszti magát;
 * a már létező UI-feladat törli, új feladat és új memóriafoglalás nélkül. */
typedef struct {
    portMUX_TYPE lock;
    TaskHandle_t handle;
    bool finished;
    bool reaping;
} app_intercom_task_t;

#define APP_INTERCOM_TASK_INITIALIZER { .lock = portMUX_INITIALIZER_UNLOCKED }

static inline void app_intercom_task_created(app_intercom_task_t *task, TaskHandle_t handle)
{
    portENTER_CRITICAL(&task->lock);
    task->handle = handle;
    portEXIT_CRITICAL(&task->lock);
}

static inline void app_intercom_task_finish(app_intercom_task_t *task)
{
    portENTER_CRITICAL(&task->lock);
    task->finished = true;
    portEXIT_CRITICAL(&task->lock);
    /* A külső törlő a suspend előtti versenyben is előbb felfüggeszti a workert. */
    for (;;) {
        vTaskSuspend(NULL);
    }
}

static inline bool app_intercom_task_reap(app_intercom_task_t *task)
{
    portENTER_CRITICAL(&task->lock);
    TaskHandle_t handle = task->finished && !task->reaping ? task->handle : NULL;
    if (handle) {
        task->handle = NULL;
        task->reaping = true;
    }
    portEXIT_CRITICAL(&task->lock);
    if (!handle)
        return false;
    /* A worker ekkor már minden socketet, kodeket és képpuffert felszabadított. */
    vTaskDeleteWithCaps(handle);
    portENTER_CRITICAL(&task->lock);
    task->finished = false;
    task->reaping = false;
    portEXIT_CRITICAL(&task->lock);
    return true;
}
