#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned,size_t);
int xQueueSend(QueueHandle_t,const void *,TickType_t);
int xQueueReceive(QueueHandle_t,void *,TickType_t);
unsigned uxQueueMessagesWaiting(QueueHandle_t);
