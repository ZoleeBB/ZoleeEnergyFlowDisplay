/* A RAM eseménynapló kötegelt, nem blokkoló mentése SD-re. */
#include "app_event_store.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "app_events.h"
#include "app_storage.h"

#define STORE_FILE APP_EVENT_STORE_DIR "/events.log"
#define STORE_PREV APP_EVENT_STORE_DIR "/events-prev.log"
/* Egy sor felső hossza: időbélyeg, szint és a 128 bájtos szöveg. */
#define STORE_LINE 192U
/* Kötegenként ennyi bejegyzést viszünk ki; a RAM-puffer ennél nem nagyobb. */
#define STORE_CHUNK 20U

static uint32_t s_written_serial;
static int64_t s_next_write_us;
static int s_day = -1;

static const char *level_mark(app_event_level_t level)
{
    return level == APP_EVENT_ERROR ? "HIBA" : level == APP_EVENT_WARNING ? "FIGYELEM" : "INFO";
}

/* A fali idő naptári napja; -1, ha az óra még nincs szinkronban. */
static int local_day(time_t wall, char date[11])
{
    struct tm local;
    if (wall < 1600000000 || !localtime_r(&wall, &local))
    {
        if (date)
        {
            snprintf(date, 11, "%s", "0000-00-00");
        }
        return -1;
    }
    if (date)
    {
        strftime(date, 11, "%Y-%m-%d", &local);
    }
    return local.tm_year * 366 + local.tm_yday;
}

/*
 * A napi fájl fejléce a dátumot is tartalmazza, így újraindítás után is
 * eldönthető, hogy a meglévő fájl a mai naphoz tartozik-e.
 */
static bool file_day_matches(const char *date)
{
    FILE *file = fopen(STORE_FILE, "r");
    if (!file)
    {
        return false;
    }
    char header[64] = {0};
    bool match = fgets(header, sizeof(header), file) && strstr(header, date) != NULL;
    fclose(file);
    return match;
}

/* Napváltáskor és méretkorlátnál: az előző fájl eldobása, a mai léptetése. */
static void rotate(const char *date)
{
    remove(STORE_PREV);
    rename(STORE_FILE, STORE_PREV);
    FILE *file = fopen(STORE_FILE, "w");
    if (file)
    {
        fprintf(file, "# Zolee eseménynapló %s\n", date);
        fclose(file);
    }
}

/* A naplókönyvtár létrehozása; ha már megvan, a hiba figyelmen kívül marad. */
static void ensure_dir(void)
{
    struct stat info;
    if (stat(APP_EVENT_STORE_DIR, &info) == 0 && S_ISDIR(info.st_mode))
    {
        return;
    }
    (void)mkdir(APP_EVENT_STORE_DIR, 0775);
}

static long file_size(void)
{
    struct stat info;
    return stat(STORE_FILE, &info) || !S_ISREG(info.st_mode) ? 0 : (long)info.st_size;
}

/*
 * Egy napló sora. A bejegyzést nem másoljuk: a hívó a saját példányának
 * mezőit adja át, így a verem egy sornyi puffernél többet nem visel.
 */
static void format_line(char out[STORE_LINE], int64_t epoch_s, app_event_level_t level,
                        const char *text)
{
    /* Időszinkron előtti esemény: a sor így is azonosítható marad. */
    char stamp[20] = "0000-00-00 00:00:00";
    struct tm local;
    time_t when = (time_t)epoch_s;
    if (epoch_s > 1600000000 && localtime_r(&when, &local))
    {
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    }
    snprintf(out, STORE_LINE, "%s %-8s %.128s\n", stamp, level_mark(level), text);
}

bool app_event_store_step(int64_t now_us, time_t wall)
{
    if (app_storage_is_cardless() || !app_storage_is_ok())
    {
        return false;
    }

    /*
     * A döntéshez elég a sorszám: egyetlen bejegyzés sem kerül a verembe.
     * (Korábban egy 20 elemű tömb volt itt, ami a 3 KiB-os karbantartó feladat
     * vermét túlcsordította, és a készülék indulás közben újraindult.)
     */
    uint32_t latest = app_events_latest_serial();
    if (latest <= s_written_serial)
    {
        return false;
    }
    /* Ritka eseményeknél nem kapkodunk; sok újnál viszont nem várunk. */
    if (latest - s_written_serial < APP_EVENT_STORE_BATCH && now_us < s_next_write_us)
    {
        return false;
    }
    /* A kártyát csak akkor vesszük el, ha épp szabad: a témabetöltés, a
     * telepítés és a statisztikamentés mindig előbbre való. Ha a kijelző már
     * jelezte, hogy neki kell a kártya, meg sem próbáljuk. */
    if (app_storage_priority_requested() || !app_storage_try_lock())
    {
        return false;
    }

    ensure_dir();
    char date[11];
    int day = local_day(wall, date);
    if (day >= 0 && day != s_day)
    {
        /*
         * Induláskor `s_day` még ismeretlen, de a meglévő fájl a mai naphoz
         * tartozhat: ilyenkor a fejléc dátuma dönt, különben minden
         * újraindítás fölöslegesen léptetné a naplót, és a reggeli bejegyzések
         * egy második újraindítással el is vesznének.
         */
        if (!file_day_matches(date))
        {
            rotate(date);
        }
        s_day = day;
    }
    if (file_size() >= (long)APP_EVENT_STORE_MAX_BYTES)
    {
        rotate(date);
    }

    FILE *file = fopen(STORE_FILE, "a");
    bool written = false;
    if (file)
    {
        /* Egyszerre egy bejegyzés: a verem így egy sornyi puffert visel. */
        app_event_entry_t entry;
        char line[STORE_LINE];
        uint32_t serial = s_written_serial;
        for (unsigned moved = 0; moved < STORE_CHUNK; moved++)
        {
            uint32_t dropped = 0;
            if (app_events_since(serial, &entry, 1, &dropped) != 1)
            {
                break;
            }
            if (dropped)
            {
                char gap[64];
                snprintf(gap, sizeof(gap), "%u esemény kimaradt a RAM-pufferből",
                         (unsigned)dropped);
                format_line(line, entry.epoch_s, APP_EVENT_WARNING, gap);
                fputs(line, file);
            }
            format_line(line, entry.epoch_s, entry.level, entry.text);
            fputs(line, file);
            serial = entry.serial;
        }
        written = fclose(file) == 0 && serial != s_written_serial;
        if (written)
        {
            s_written_serial = serial;
        }
    }
    s_next_write_us = now_us + APP_EVENT_STORE_INTERVAL_US;
    app_storage_unlock();
    return written;
}
