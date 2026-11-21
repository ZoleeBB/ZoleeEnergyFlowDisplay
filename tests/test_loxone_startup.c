/* Minden erőforráshatárnál megszakítjuk a valódi indítást. A hálózati
 * ciklust a protokollteszt vizsgálja, itt a létrehozó és a publikálás a tárgy. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/task.h"

static void *tracked_calloc(size_t count, size_t size);
static void tracked_free(void *ptr);
static int start_task(void *context);
#define calloc tracked_calloc
#define free tracked_free
#define xTaskCreatePinnedToCore(fn, name, stack, arg, priority, handle, core) start_task(arg)
#include "../main/app_loxone.c"
#undef xTaskCreatePinnedToCore
#undef free
#undef calloc

static unsigned step, stop_at, live, tasks, intercom_attempts, home_reads;
static bool gate_held;
static worker_context_t *worker_arg;

static bool resource_fails(void)
{
    /* A web és a képernyő mindaddig üres állapotot lát, amíg a kliens
     * nem tudott munkafeladatot indítani. Az újrabelépés itt sem duplikál. */
    assert(!s_lock && !s_messages && !s_requests && !s_home_commands && !s_home);
    assert(!app_loxone_web_state());
    assert(app_loxone_start(38) == ESP_ERR_INVALID_STATE);
    return ++step == stop_at;
}

static void *tracked_calloc(size_t count, size_t size)
{
    if (resource_fails()) return NULL;
    void *ptr = calloc(count, size);
    assert(ptr);
    ++live;
    return ptr;
}

static void tracked_free(void *ptr)
{
    if (ptr) {
        assert(live);
        --live;
        free(ptr);
    }
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return tracked_calloc(1, sizeof(pthread_mutex_t)); }
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(!gate_held); tracked_free(mutex); }
QueueHandle_t xQueueCreate(unsigned count, size_t size)
{
    assert(count && size);
    return tracked_calloc(1, 1);
}
void vQueueDelete(QueueHandle_t queue) { tracked_free(queue); }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    assert(mutex && timeout == portMAX_DELAY && !gate_held);
    gate_held = true;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex && gate_held);
    gate_held = false;
    return pdTRUE;
}
esp_err_t app_loxone_home_store_load(lx_home_config_t *home)
{
    assert(home);
    ++home_reads;
    if (resource_fails()) return ESP_ERR_NO_MEM;
    return ESP_ERR_NOT_FOUND;
}
void lx_config_defaults(lx_config_t *cfg) { memset(cfg, 0, sizeof(*cfg)); }
esp_err_t app_loxone_store_load(lx_config_t *cfg)
{
    if (resource_fails()) return ESP_ERR_NO_MEM;
    strcpy(cfg->host, "miniserver.local");
    strcpy(cfg->password, "teszt");
    return ESP_OK;
}
cJSON *lx_config_json(const lx_config_t *cfg)
{
    assert(cfg == &s_saved && !strcmp(cfg->host, "miniserver.local"));
    return cJSON_CreateObject();
}
void app_state_get_runtime(app_runtime_state_t *out)
{
    *out = (app_runtime_state_t){.data_recept_mode = APP_DATA_LOXONE, .energy_flow_disabled = true};
}
static int start_task(void *arg)
{
    worker_context_t *ctx = arg;
    assert(gate_held && ctx && ctx->lock);
    assert(!strcmp(ctx->config.host, "miniserver.local"));
    if (resource_fails()) return 0;
    ++tasks;
    worker_arg = ctx;
    return pdPASS;
}
esp_err_t app_intercom_start(void)
{
    assert(!gate_held && worker_arg && worker_arg->lock == s_lock);
    assert(s_saved.presence.timeout_seconds == 38 && s_password && s_energy_disabled);
    assert(s_home->intercom.speaker_volume == 55 && s_home->intercom.microphone_gain == 80);
    cJSON *web = app_loxone_web_state();
    assert(web && cJSON_IsTrue(cJSON_GetObjectItem(web, "has_password")));
    cJSON_Delete(web);
    return ++intercom_attempts == 1 ? ESP_ERR_NO_MEM : ESP_OK;
}

int main(void)
{
    /* Mutex, három sor, két saját blokk, két tárolóolvasás és taskindítás. */
    for (stop_at = 1; stop_at <= 9; ++stop_at) {
        step = 0;
        assert(app_loxone_start(38) == ESP_ERR_NO_MEM);
        assert(!live && !gate_held && !tasks && !intercom_attempts && !app_loxone_web_state());
    }
    stop_at = 0;
    step = 0;
    assert(app_loxone_start(38) == ESP_ERR_NO_MEM);
    assert(step == 9 && live == 6 && tasks == 1 && intercom_attempts == 1);
    unsigned reads = home_reads;
    assert(app_loxone_start(38) == ESP_OK && intercom_attempts == 2);
    assert(app_loxone_start(38) == ESP_OK && intercom_attempts == 2);
    assert(live == 6 && home_reads == reads && tasks == 1);
    vSemaphoreDelete(s_lock);
    vQueueDelete(s_messages);
    vQueueDelete(s_requests);
    vQueueDelete(s_home_commands);
    tracked_free(s_home);
    tracked_free(worker_arg);
    assert(!live);
    puts("PASS: Loxone startup OOM at nine boundaries, complete configuration publication, single worker and intercom retry");
}
