#pragma once
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
#define pdTRUE 1
int xTaskCreate(void (*function)(void *),
                const char *name,
                unsigned stack,
                void *arg,
                unsigned priority,
                void *handle);
void vTaskDelay(TickType_t ticks);
void xTaskNotifyGive(TaskHandle_t task);
unsigned ulTaskNotifyTake(int clear, TickType_t ticks);

void vTaskDelete(TaskHandle_t task);
