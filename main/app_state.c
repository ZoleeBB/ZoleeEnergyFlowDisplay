#include "app_state.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static SemaphoreHandle_t s_mutex;
static app_runtime_state_t s_runtime;
static app_energy_sample_t s_sample;

void app_state_init(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
    memset(&s_runtime, 0, sizeof(s_runtime));
    memset(&s_sample, 0, sizeof(s_sample));
}
static void lock(void){ xSemaphoreTake(s_mutex, portMAX_DELAY); }
static void unlock(void){ xSemaphoreGive(s_mutex); }
void app_state_set_sd_ok(bool v){ lock(); s_runtime.sd_ok=v; unlock(); }
void app_state_set_config_ok(bool v){ lock(); s_runtime.config_ok=v; unlock(); }
void app_state_set_wifi_connected(bool v){ lock(); s_runtime.wifi_connected=v; unlock(); }
void app_state_set_time_synced(bool v){ lock(); s_runtime.time_synced=v; unlock(); }
void app_state_set_mqtt_connected(bool v){ lock(); s_runtime.mqtt_connected=v; if(v)s_runtime.mqtt_connected_monotonic_us=esp_timer_get_time(); unlock(); }
void app_state_set_sample(const app_energy_sample_t *s){ if(!s)return; lock(); s_sample=*s; s_runtime.has_valid_sample=true; s_runtime.last_valid_packet_monotonic_us=s->received_monotonic_us; s_runtime.last_valid_packet_epoch_s=s->received_epoch_s; unlock(); }
void app_state_get_runtime(app_runtime_state_t *o){ if(!o)return; lock(); *o=s_runtime; unlock(); }
bool app_state_get_sample(app_energy_sample_t *o){ bool valid; lock(); valid=s_runtime.has_valid_sample; if(o&&valid)*o=s_sample; unlock(); return valid; }
