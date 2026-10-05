#pragma once
/* A szimulátor egyetlen UI szálon fut; nincs párhuzamos producer. */
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t m, unsigned t) { (void)m; (void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t m) { (void)m; return 1; }
