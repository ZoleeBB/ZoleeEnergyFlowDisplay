#pragma once
/* Egyszerű, teszthez elég FreeRTOS üzenetsor. */
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
typedef struct queue_stub *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
