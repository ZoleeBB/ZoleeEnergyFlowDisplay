#pragma once
#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t function, const char *name, uint32_t stack,
                              void *argument, UBaseType_t priority, TaskHandle_t *handle, unsigned caps);
void vTaskDeleteWithCaps(TaskHandle_t handle);
