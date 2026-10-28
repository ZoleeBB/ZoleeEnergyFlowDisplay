#pragma once
/*
 * Feladatverem a DMA-ra alkalmatlan RTC-területről.
 *
 * A belső RAM több részre oszlik, és csak egy része DMA-képes. Az SD-vezérlő
 * és a kriptográfiai gyorsító kizárólag onnan tud foglalni, a feladatvermek
 * viszont alapértelmezésben ugyanabból a keretből mennek – miközben az RTC
 * terület (31 KiB) és a TCM (7 KiB) teljesen üresen áll.
 *
 * Az itt létrehozott feladatok verme és vezérlőblokkja az RTC-területre kerül,
 * így a DMA-képes keret felszabadul anélkül, hogy bármelyik verem kisebb
 * lenne. Az RTC memória nincs a gyorsítótár mögött, ezért – a PSRAM-mal
 * ellentétben – flash-írás (NVS) közben is biztonságosan használható.
 * Csak örökké futó feladathoz való: megszűnéshez `vTaskDeleteWithCaps` kellene.
 * Ha az RTC-terület betelt, a szokásos belső RAM-os foglalás a tartalék.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(__has_include)
#if __has_include("esp_heap_caps.h") && __has_include("freertos/idf_additions.h")
#define APP_TASK_MEM_LOW_POWER 1
#endif
#endif

#ifdef APP_TASK_MEM_LOW_POWER
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
static inline BaseType_t app_task_create_lp(TaskFunction_t function, const char *name,
                                            uint32_t stack_bytes, void *argument,
                                            UBaseType_t priority, TaskHandle_t *handle)
{
    if (xTaskCreateWithCaps(function, name, stack_bytes, argument, priority, handle,
                            MALLOC_CAP_RTCRAM) == pdPASS)
        return pdPASS;
    return xTaskCreate(function, name, stack_bytes, argument, priority, handle);
}
#else
/* Szimulátor és gazdagépes tesztek: nincs RTC-terület, marad a szokásos út. */
#define app_task_create_lp(function, name, stack_bytes, argument, priority, handle) \
    xTaskCreate(function, name, stack_bytes, argument, priority, handle)
#endif
