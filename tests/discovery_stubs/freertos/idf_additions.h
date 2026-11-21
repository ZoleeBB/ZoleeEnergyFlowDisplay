#pragma once
#include "freertos/task.h"
typedef void *TaskHandle_t;
int xTaskCreateWithCaps(void (*function)(void *), const char *name, unsigned stack,
                        void *arg, unsigned priority, TaskHandle_t *handle, unsigned caps);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void vTaskSuspend(TaskHandle_t handle);
void vTaskDeleteWithCaps(TaskHandle_t handle);
