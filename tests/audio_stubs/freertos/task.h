#pragma once
#include "FreeRTOS.h"
void vTaskDelay(unsigned delay);
int xTaskCreatePinnedToCore(void (*worker)(void *), const char *name, unsigned stack,
                           void *argument, unsigned priority, void *handle, unsigned core);

void vTaskDelete(void *task);

typedef void *TaskHandle_t;
void vTaskSuspend(TaskHandle_t task);
