/* A két transzport ugyanazon atomi ellenőrzési és feldolgozási úton érkezik. */
#include "app_ingest.h"
#include "app_payload.h"
#include "app_state.h"
#include "app_stats.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
static SemaphoreHandle_t s_mutex;
static app_data_mode_t s_mode;
esp_err_t app_ingest_init(app_data_mode_t mode)
{
    if (s_mutex) return ESP_ERR_INVALID_STATE;
    s_mode = mode;
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t app_ingest_process(app_data_mode_t source, const char *data, size_t length, char *error, size_t error_length)
{
    if (!s_mutex || source != s_mode) {
        if (error && error_length) snprintf(error, error_length, "Data receiver unavailable");
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    app_energy_sample_t sample;
    esp_err_t result = app_payload_parse(data, length, &sample, error, error_length);
    if (result == ESP_OK) {
        app_stats_process_sample(&sample);
        app_state_set_sample(&sample);
    }
    xSemaphoreGive(s_mutex);
    return result;
}
