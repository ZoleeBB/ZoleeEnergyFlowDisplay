#pragma once
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout);
int xQueueOverwrite(QueueHandle_t queue, const void *data);

void vQueueDelete(QueueHandle_t queue);
