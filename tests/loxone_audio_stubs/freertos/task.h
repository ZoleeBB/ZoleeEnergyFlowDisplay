#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t handle);
void vTaskDelay(TickType_t ticks);
