/*
 * Rövid, RAM-ban tárolt eseménynapló.
 *
 * Körkörös pufferként működik, így nem ír folyamatosan az SD-kártyára.
 * A helyi weboldal ezt a puffert jeleníti meg.
 */
#include "app_events.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* A puffer és a mutatók közös elérését mutex védi. */
static SemaphoreHandle_t s_mutex;
static app_event_entry_t s_entries[APP_EVENT_LOG_CAPACITY];
static size_t s_count;
static size_t s_next;

/* Mindkét nyelvet rögzítjük: a config előtt keletkezett esemény is helyesen olvasható. */
static const char *english_format(const char *format)
{
    static const struct
    {
        const char *hu;
        const char *en;
    } messages[] = {
        {"OTA firmware ellenőrzése sikeres; verzió véglegesítve.",
         "OTA verification passed; firmware accepted."},
        {"OTA ellenőrzési időtúllépés; visszaállás az előző firmware-re.",
         "OTA verification timed out; rolling back to previous firmware."},
        {"Wi-Fi kapcsolat megszakadt.", "Wi-Fi disconnected."},
        {"Wi-Fi kapcsolat létrejött.", "Wi-Fi connected."},
        {"Új OTA firmware feltöltve; újraindítás következik.",
         "New OTA firmware uploaded; restarting."},
        {"Webes információs és OTA felület elindult.", "Local information and OTA server started."},
        {"Konfiguráció betöltve.", "Configuration loaded."},
        {"NTP időszinkron sikeres.", "NTP time synchronized."},
        {"Web/OTA felület nem indult el: %s", "Web/OTA server failed to start: %s"},
        {"Rendszerindítás befejeződött.", "System startup completed."},
        {"SD-kártya csatlakoztatva.", "SD card mounted."},
        {"SD-kártya csatlakoztatási hiba.", "SD card mount failed."},
        {"SD-kártya hiba: %s", "SD card error: %s"},
        {"SD-kártya kapcsolat helyreállt.", "SD card recovered."},
        {"Statisztika helyreállítva: %s", "Statistics recovered: %s"},
        {"Új nap indult: %s", "New day started: %s"},
        {"Sérült mai statisztika archiválva; a nap nulláról indul.",
         "Corrupt statistics for today archived; starting the day from zero."},
        {"Sérült tegnapi statisztika archiválva; összehasonlítás kikapcsolva.",
         "Corrupt statistics for yesterday archived; comparison disabled."},
        {"Hibás MQTT adatcsomag elutasítva (mező: %s).",
         "Invalid MQTT payload rejected (field: %s)."},
        {"MQTT payload mérete érvénytelen: %d byte", "Invalid MQTT payload size: %d bytes"},
        {"MQTT feliratkozás sikertelen: %s", "MQTT subscription failed: %s"},
        {"MQTT broker kapcsolat létrejött.", "MQTT broker connected."},
        {"MQTT broker kapcsolat megszakadt.", "MQTT broker disconnected."},
        {"MQTT transport hiba: %s, socket=%d", "MQTT transport error: %s, socket=%d"},
        {"NTP újraszinkron sikertelen; belső óra tovább fut.",
         "NTP resynchronization failed; internal clock continues."},
    };
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); i++)
        if (strcmp(format, messages[i].hu) == 0)
            return messages[i].en;
    return format;
}

/* Az eseménypuffer alaphelyzetbe állítása. */
void app_events_init(void)
{
    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
    }

    s_count = 0;
    s_next = 0;
    memset(s_entries, 0, sizeof(s_entries));
}

/* Formázott esemény hozzáadása a körkörös pufferhez. */
void app_events_add(app_event_level_t level, const char *fmt, ...)
{
    if (!s_mutex || !fmt)
    {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    app_event_entry_t *entry = &s_entries[s_next];
    memset(entry, 0, sizeof(*entry));
    entry->level = level;
    entry->epoch_s = (int64_t)time(NULL);

    va_list args;
    va_start(args, fmt);
    va_list english_args;
    va_copy(english_args, args);
    vsnprintf(entry->text, sizeof(entry->text), fmt, args);
    vsnprintf(entry->text_en, sizeof(entry->text_en), english_format(fmt), english_args);
    va_end(english_args);
    va_end(args);

    s_next = (s_next + 1) % APP_EVENT_LOG_CAPACITY;
    if (s_count < APP_EVENT_LOG_CAPACITY)
    {
        s_count++;
    }

    xSemaphoreGive(s_mutex);
}

/*
 * A legfrissebb események konzisztens másolatát adja vissza.
 * Ha a hívó kisebb tömböt ad, a legrégebbi elemek maradnak ki.
 */
size_t app_events_snapshot(app_event_entry_t *out, size_t max_items)
{
    if (!out || !max_items || !s_mutex)
    {
        return 0;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t count = s_count < max_items ? s_count : max_items;
    size_t start = (s_next + APP_EVENT_LOG_CAPACITY - s_count) % APP_EVENT_LOG_CAPACITY;

    size_t skip = s_count > count ? s_count - count : 0;
    start = (start + skip) % APP_EVENT_LOG_CAPACITY;

    for (size_t i = 0; i < count; i++)
    {
        out[i] = s_entries[(start + i) % APP_EVENT_LOG_CAPACITY];
    }

    xSemaphoreGive(s_mutex);
    return count;
}
