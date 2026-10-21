#pragma once
#include "freertos/FreeRTOS.h"
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,void *);
int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,void *,int);
void vTaskDelay(TickType_t);
void vTaskDelete(void *);
