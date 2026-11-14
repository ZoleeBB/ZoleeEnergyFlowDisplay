#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, size_t size);
int xQueueSend(QueueHandle_t queue, const void *data, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *data, TickType_t timeout);
void vQueueDelete(QueueHandle_t queue);
