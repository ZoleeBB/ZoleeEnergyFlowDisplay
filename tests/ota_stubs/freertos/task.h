#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
typedef void *TaskHandle_t;
typedef struct { bool held; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {false}
void test_ota_enter_critical(portMUX_TYPE *lock);
void test_ota_exit_critical(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) test_ota_enter_critical(lock)
#define portEXIT_CRITICAL(lock) test_ota_exit_critical(lock)
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
