/*
 * Belső flash-tartalék a beállításcsomagnak a korábban használaton kívüli
 * 7 MiB-os „storage” partíción. Fájlrendszer nélkül, két váltakozó, 1 MiB-os
 * slotban: mindig a régebbi slot íródik, így tápvesztés csak a félig írt
 * slotot ronthatja el. Csak tényleges tartalomváltozáskor ír, ezért a flash
 * kopása elhanyagolható (bootkor és beállításmentés után egy összevetés).
 *
 * A tartalék a jelszavakat is tartalmazza (ugyanúgy, ahogy a config.json is
 * az SD-n); a Loxone- és kamerajelszó eszközkulccsal titkosított marad.
 */
#include "app_backup.h"
#include "app_events.h"
#include "app_storage.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLOT_SIZE (1024U * 1024U)
#define SYNC_DELAY_MS 15000

static const char *TAG = "backup";
static SemaphoreHandle_t s_lock, s_request;

static const esp_partition_t *partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
}

/* Egy slot fejléce és hasznos adatának CRC-je együtt érvényes. */
static bool slot_read(const esp_partition_t *part, unsigned index, app_backup_slot_t *slot,
                      char **payload)
{
    uint8_t header[APP_BACKUP_SLOT_HEADER];
    size_t offset = (size_t)index * SLOT_SIZE;
    if (esp_partition_read(part, offset, header, sizeof(header)) != ESP_OK ||
        !app_backup_slot_decode(header, SLOT_SIZE, slot))
        return false;
    char *data = heap_caps_malloc(slot->length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data)
        data = malloc(slot->length + 1);
    if (!data)
        return false;
    bool ok = esp_partition_read(part, offset + APP_BACKUP_SLOT_HEADER, data, slot->length) == ESP_OK &&
              app_backup_crc32(0, data, slot->length) == slot->payload_crc;
    data[slot->length] = 0;
    if (ok && payload)
        *payload = data;
    else
        free(data);
    return ok;
}

/* Mindkét slot beolvasása; a frissebb érvényes indexét adja (-1: nincs). */
static int latest(const esp_partition_t *part, app_backup_slot_t slots[2], bool valid[2])
{
    for (unsigned i = 0; i < 2; i++)
        valid[i] = slot_read(part, i, &slots[i], NULL);
    return app_backup_slot_pick(&slots[0], valid[0], &slots[1], valid[1]);
}

static bool ensure_lock(void)
{
    if (!s_lock)
        s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL;
}

esp_err_t app_backup_flash_info(app_backup_flash_info_t *out)
{
    const esp_partition_t *part = partition();
    if (!out)
        return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!part || part->size < 2 * SLOT_SIZE)
        return ESP_ERR_NOT_FOUND;
    if (!ensure_lock())
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_backup_slot_t slots[2];
    bool valid[2];
    int index = latest(part, slots, valid);
    xSemaphoreGive(s_lock);
    if (index < 0)
        return ESP_OK;
    out->available = true;
    out->sequence = slots[index].sequence;
    out->length = slots[index].length;
    out->epoch_s = slots[index].epoch_s;
    return ESP_OK;
}

esp_err_t app_backup_flash_load(char **json, size_t *length)
{
    const esp_partition_t *part = partition();
    if (!json || !length)
        return ESP_ERR_INVALID_ARG;
    *json = NULL;
    *length = 0;
    if (!part || part->size < 2 * SLOT_SIZE)
        return ESP_ERR_NOT_FOUND;
    if (!ensure_lock())
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_backup_slot_t slots[2];
    bool valid[2];
    int index = latest(part, slots, valid);
    esp_err_t result = ESP_ERR_NOT_FOUND;
    if (index >= 0 && slot_read(part, (unsigned)index, &slots[index], json))
    {
        *length = slots[index].length;
        result = ESP_OK;
    }
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t app_backup_flash_save(const char *json, size_t length, bool *written)
{
    const esp_partition_t *part = partition();
    if (written)
        *written = false;
    if (!json || !length || length > APP_BACKUP_MAX)
        return ESP_ERR_INVALID_ARG;
    if (!part || part->size < 2 * SLOT_SIZE)
        return ESP_ERR_NOT_FOUND;
    if (!ensure_lock())
        return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_backup_slot_t slots[2];
    bool valid[2];
    int index = latest(part, slots, valid);
    uint32_t crc = app_backup_crc32(0, json, length);
    esp_err_t result = ESP_OK;
    if (index >= 0 && slots[index].length == length && slots[index].payload_crc == crc)
    {
        xSemaphoreGive(s_lock);
        return ESP_OK; /* Változatlan: nincs flash-írás. */
    }
    unsigned target = index < 0 ? 0 : (unsigned)(1 - index);
    app_backup_slot_t slot = {
        .magic = APP_BACKUP_SLOT_MAGIC,
        .version = APP_BACKUP_VERSION,
        .sequence = index < 0 ? 1 : slots[index].sequence + 1,
        .length = (uint32_t)length,
        .payload_crc = crc,
        .epoch_s = (int64_t)time(NULL),
    };
    uint8_t header[APP_BACKUP_SLOT_HEADER];
    app_backup_slot_encode(&slot, header);
    size_t offset = (size_t)target * SLOT_SIZE;
    size_t erase = (APP_BACKUP_SLOT_HEADER + length + 4095U) & ~4095U;
    /* Előbb a hasznos adat, a fejléc utoljára: félbeszakadt írásnál a slot érvénytelen. */
    result = esp_partition_erase_range(part, offset, erase);
    if (result == ESP_OK)
        result = esp_partition_write(part, offset + APP_BACKUP_SLOT_HEADER, json, length);
    if (result == ESP_OK)
        result = esp_partition_write(part, offset, header, sizeof(header));
    xSemaphoreGive(s_lock);
    if (result == ESP_OK && written)
        *written = true;
    return result;
}

esp_err_t app_backup_flash_restore_sd(unsigned *files, char error[APP_BACKUP_ERROR_SIZE])
{
    char *json = NULL;
    size_t length = 0;
    esp_err_t result = app_backup_flash_load(&json, &length);
    if (result != ESP_OK)
    {
        if (error)
            snprintf(error, APP_BACKUP_ERROR_SIZE, "%s", "Nincs érvényes belső tartalék.");
        return result;
    }
    result = app_storage_lock();
    if (result == ESP_OK)
    {
        result = app_backup_restore(APP_SD_MOUNT_POINT, json, length, files, error);
        app_storage_unlock();
    }
    else if (error)
        snprintf(error, APP_BACKUP_ERROR_SIZE, "%s", "Az SD-kártya nem érhető el.");
    free(json);
    return result;
}

/* Háttérszinkron: a kérések összevonva, késleltetve futnak, hogy egy
 * beállítássorozat (pl. több Loxone-szakasz mentése) csak egyszer írjon. */
static void sync_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        xSemaphoreTake(s_request, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(SYNC_DELAY_MS));
        while (xSemaphoreTake(s_request, 0) == pdTRUE)
        {
        }
        if (!app_storage_is_ok() || app_storage_lock() != ESP_OK)
            continue;
        char error[APP_BACKUP_ERROR_SIZE] = {0};
        const esp_app_desc_t *app = esp_app_get_description();
        char *json = app_backup_export(APP_SD_MOUNT_POINT, true, app ? app->version : "", error);
        app_storage_unlock();
        if (!json)
        {
            ESP_LOGW(TAG, "Backup export skipped: %s", error);
            continue;
        }
        bool written = false;
        esp_err_t result = app_backup_flash_save(json, strlen(json), &written);
        free(json);
        if (result != ESP_OK)
            app_events_add(APP_EVENT_WARNING, "A belső beállítás-tartalék írása sikertelen: %s",
                           esp_err_to_name(result));
        else if (written)
            app_events_add(APP_EVENT_INFO, "Belső beállítás-tartalék frissítve.");
    }
}

esp_err_t app_backup_flash_start(void)
{
    if (!ensure_lock())
        return ESP_ERR_NO_MEM;
    if (!s_request)
    {
        s_request = xSemaphoreCreateBinary();
        if (!s_request)
            return ESP_ERR_NO_MEM;
        if (xTaskCreate(sync_task, "backup_sync", 5120, NULL, 2, NULL) != pdPASS)
            return ESP_ERR_NO_MEM;
    }
    app_backup_flash_request();
    return ESP_OK;
}

void app_backup_flash_request(void)
{
    if (s_request)
        xSemaphoreGive(s_request);
}
