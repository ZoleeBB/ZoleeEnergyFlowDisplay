#pragma once
#include "task.h"
int xTaskCreatePinnedToCoreWithCaps(void (*worker)(void *), const char *name, unsigned stack,
                                  void *argument, unsigned priority, void *handle, unsigned core, unsigned caps);
void vTaskDeleteWithCaps(void *task);
