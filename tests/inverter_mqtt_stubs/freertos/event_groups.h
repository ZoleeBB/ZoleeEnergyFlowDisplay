#pragma once
#include <stdint.h>
#define pdFALSE 0
#define BIT0 1U
#define BIT1 2U
#define BIT2 4U
typedef uint32_t EventBits_t;
typedef EventBits_t *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t g);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t mask, int clear, int all, unsigned timeout);
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits);
