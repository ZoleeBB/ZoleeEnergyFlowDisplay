/*
 * Az eseménynapló SD-re mentésének tényleges logikája: kötegelés, foglalt
 * kártya, napváltás, méretkorlát és a kimaradt bejegyzések jelzése.
 */
#include "app_event_store.h"
#include "app_events.h"
#include "app_storage.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <assert.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* A RAM-napló mutexe a hoszton egyszerű jelző. */
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
int xSemaphoreTake(SemaphoreHandle_t handle, unsigned wait)
{
    (void)handle;
    (void)wait;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t handle)
{
    (void)handle;
    return 1;
}

/* A tárzár viselkedését a teszt vezérli. */
static bool g_card_ok = true, g_cardless, g_busy, g_locked, g_priority;

bool app_storage_is_ok(void) { return g_card_ok; }
bool app_storage_priority_requested(void) { return g_priority; }
bool app_storage_is_cardless(void) { return g_cardless; }
bool app_storage_try_lock(void)
{
    if (g_busy)
        return false;
    assert(!g_locked); /* A mentés soha nem veheti fel kétszer. */
    g_locked = true;
    return true;
}
void app_storage_unlock(void)
{
    assert(g_locked);
    g_locked = false;
}

static char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    char *data = malloc((size_t)length + 1);
    assert(data);
    size_t read = fread(data, 1, (size_t)length, file);
    fclose(file);
    data[read] = 0;
    if (size)
        *size = read;
    return data;
}

static unsigned count_lines(const char *text, const char *needle)
{
    unsigned found = 0;
    for (const char *at = text; (at = strstr(at, needle)); at += strlen(needle))
        found++;
    return found;
}

#define LOG_FILE APP_EVENT_STORE_DIR "/events.log"
#define LOG_PREV APP_EVENT_STORE_DIR "/events-prev.log"

/* 2026-11-02 12:00:00 UTC körüli fali idők; a nap hossza 86400 s. */
static const time_t DAY_ONE = 1793620800;

/*
 * Újraindítás utáni folytatás: a meglévő, mai naplót nem szabad fölöslegesen
 * léptetni, mert akkor a reggeli bejegyzések egy második újraindítással
 * elvesznének. A tesztet külön folyamatként futtatjuk, hogy a modul statikus
 * állapota tényleg nulláról induljon.
 */
static int resume_after_restart(void)
{
    app_events_init();
    char *before = read_file(LOG_FILE, NULL);
    assert(before && strstr(before, "Korlát utáni esemény."));
    free(before);

    app_events_add(APP_EVENT_INFO, "Újraindítás utáni esemény.");
    assert(app_event_store_step(1000000, DAY_ONE + 2 * 86400));

    char *after = read_file(LOG_FILE, NULL);
    assert(after);
    /* Ugyanaz a nap: a korábbi sorok a helyükön maradtak, nincs forgatás. */
    assert(strstr(after, "Korlát utáni esemény."));
    assert(strstr(after, "INFO     Újraindítás utáni esemény."));
    assert(count_lines(after, "# Zolee eseménynapló") == 1);
    free(after);

    /* A következő nap viszont már léptet. */
    app_events_add(APP_EVENT_INFO, "Új nap első eseménye.");
    assert(app_event_store_step(1000000 + APP_EVENT_STORE_INTERVAL_US, DAY_ONE + 3 * 86400));
    after = read_file(LOG_FILE, NULL);
    assert(after && strstr(after, "Új nap első eseménye.") &&
           !strstr(after, "Újraindítás utáni esemény."));
    free(after);
    assert(!g_locked);
    puts("PASS: stored log survives a restart without an extra rotation");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "resume"))
    {
        return resume_after_restart();
    }
    app_events_init();

    /* Esemény nélkül nem keletkezik fájl, és a zárat sem kérjük el. */
    assert(!app_event_store_step(0, DAY_ONE));
    assert(!read_file(LOG_FILE, NULL));

    /* Egyetlen esemény: a kötegméret alatt vagyunk, de az első kiírás azonnal
     * mehet, mert még nem írtunk semmit. */
    app_events_add(APP_EVENT_INFO, "Rendszerindítás befejeződött.");
    assert(app_event_store_step(1000000, DAY_ONE));
    size_t size = 0;
    char *text = read_file(LOG_FILE, &size);
    assert(text);
    assert(strstr(text, "# Zolee eseménynapló 2026-11-02"));
    assert(strstr(text, "INFO     Rendszerindítás befejeződött."));
    free(text);

    /* A következő esemény nem írja ki magát azonnal: ritka eseménynél az
     * időzítés dönt, addig a RAM-ban vár. */
    app_events_add(APP_EVENT_WARNING, "Wi-Fi kapcsolat megszakadt.");
    assert(!app_event_store_step(2000000, DAY_ONE));
    text = read_file(LOG_FILE, NULL);
    assert(!strstr(text, "Wi-Fi kapcsolat megszakadt."));
    free(text);

    /* Foglalt kártyánál kimarad a kiírás, az esemény nem vész el. */
    g_busy = true;
    assert(!app_event_store_step(1000000 + APP_EVENT_STORE_INTERVAL_US, DAY_ONE));
    g_busy = false;
    assert(app_event_store_step(1000000 + APP_EVENT_STORE_INTERVAL_US, DAY_ONE));
    text = read_file(LOG_FILE, NULL);
    assert(strstr(text, "FIGYELEM Wi-Fi kapcsolat megszakadt."));
    free(text);

    /* Ha a kijelző a kártyára vár, a napló meg sem próbálja elvenni. */
    app_events_add(APP_EVENT_INFO, "Elsőbbség alatti esemény.");
    g_priority = true;
    assert(!app_event_store_step(1000000 + 2 * APP_EVENT_STORE_INTERVAL_US, DAY_ONE));
    g_priority = false;
    assert(app_event_store_step(1000000 + 2 * APP_EVENT_STORE_INTERVAL_US, DAY_ONE));
    text = read_file(LOG_FILE, NULL);
    assert(strstr(text, "Elsőbbség alatti esemény."));
    free(text);

    /* Sok új eseménynél nem várunk az időzítésre. */
    int64_t now = 2000000 + APP_EVENT_STORE_INTERVAL_US;
    for (unsigned i = 0; i < APP_EVENT_STORE_BATCH; i++)
        app_events_add(APP_EVENT_INFO, "Köteges esemény %u", i);
    assert(app_event_store_step(now, DAY_ONE));
    text = read_file(LOG_FILE, NULL);
    assert(count_lines(text, "Köteges esemény") == APP_EVENT_STORE_BATCH);
    free(text);

    /* A RAM-puffer két kiírás között körbefordulhat: a hiányt jelezzük. */
    for (unsigned i = 0; i < APP_EVENT_LOG_CAPACITY + 5; i++)
        app_events_add(APP_EVENT_INFO, "Áradat %u", i);
    now += APP_EVENT_STORE_INTERVAL_US;
    assert(app_event_store_step(now, DAY_ONE));
    text = read_file(LOG_FILE, NULL);
    assert(strstr(text, "esemény kimaradt a RAM-pufferből"));
    assert(count_lines(text, "Áradat ") == APP_EVENT_LOG_CAPACITY);
    free(text);

    /* Napváltás: a tegnapi fájl az előzőbe kerül, az új fejlécet kap. */
    app_events_add(APP_EVENT_ERROR, "Második napi esemény.");
    now += APP_EVENT_STORE_INTERVAL_US;
    assert(app_event_store_step(now, DAY_ONE + 86400));
    text = read_file(LOG_FILE, NULL);
    assert(strstr(text, "# Zolee eseménynapló 2026-11-03"));
    assert(strstr(text, "HIBA     Második napi esemény."));
    assert(!strstr(text, "Rendszerindítás befejeződött."));
    free(text);
    text = read_file(LOG_PREV, NULL);
    assert(text && strstr(text, "Rendszerindítás befejeződött."));
    free(text);

    /* Harmadik nap: a legrégebbi napló eltűnik, mindig csak kettő marad. */
    app_events_add(APP_EVENT_INFO, "Harmadik napi esemény.");
    now += APP_EVENT_STORE_INTERVAL_US;
    assert(app_event_store_step(now, DAY_ONE + 2 * 86400));
    text = read_file(LOG_PREV, NULL);
    assert(text && strstr(text, "Második napi esemény.") &&
           !strstr(text, "Rendszerindítás befejeződött."));
    free(text);
    DIR *dir = opendir(APP_EVENT_STORE_DIR);
    unsigned files = 0;
    for (struct dirent *entry; (entry = readdir(dir));)
        if (entry->d_name[0] != '.')
            files++;
    closedir(dir);
    assert(files == 2);

    /* Méretkorlát: nap közben is fordul, a kártya nem telik tele. */
    FILE *fill = fopen(LOG_FILE, "a");
    assert(fill);
    for (unsigned i = 0; i < APP_EVENT_STORE_MAX_BYTES / 64 + 2; i++)
        fputs("..............................................................\n", fill);
    fclose(fill);
    app_events_add(APP_EVENT_INFO, "Korlát utáni esemény.");
    now += APP_EVENT_STORE_INTERVAL_US;
    assert(app_event_store_step(now, DAY_ONE + 2 * 86400));
    text = read_file(LOG_FILE, &size);
    assert(text && size < APP_EVENT_STORE_MAX_BYTES / 2);
    assert(strstr(text, "Korlát utáni esemény."));
    free(text);

    /* Hiányzó vagy hibás kártyánál nincs se zár, se írás. */
    g_card_ok = false;
    app_events_add(APP_EVENT_INFO, "Kártya nélküli esemény.");
    assert(!app_event_store_step(now + APP_EVENT_STORE_INTERVAL_US, DAY_ONE + 2 * 86400));
    g_card_ok = true;
    g_cardless = true;
    assert(!app_event_store_step(now + 2 * APP_EVENT_STORE_INTERVAL_US, DAY_ONE + 2 * 86400));
    g_cardless = false;
    text = read_file(LOG_FILE, NULL);
    assert(!strstr(text, "Kártya nélküli esemény."));
    free(text);
    /* A kártya visszatérése után a várakozó esemény kimegy. */
    assert(app_event_store_step(now + 3 * APP_EVENT_STORE_INTERVAL_US, DAY_ONE + 2 * 86400));
    text = read_file(LOG_FILE, NULL);
    assert(strstr(text, "Kártya nélküli esemény."));
    free(text);

    assert(!g_locked);
    puts("PASS: event log batching, busy card, day rotation, two-day retention, size cap and dropped entries");
    return 0;
}
