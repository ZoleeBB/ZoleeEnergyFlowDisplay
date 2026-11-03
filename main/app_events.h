#pragma once

#include <stddef.h>
#include <stdint.h>

/* A RAM eseménypufferben megtartott bejegyzések száma. */
#define APP_EVENT_LOG_CAPACITY 20

/* Egy esemény maximális szöveghossza a lezáró nulla nélkül. */
#define APP_EVENT_TEXT_MAX 128

/* Esemény súlyossági szintek. */
typedef enum
{
    APP_EVENT_INFO = 0,
    APP_EVENT_WARNING,
    APP_EVENT_ERROR
} app_event_level_t;

/* Egy időbélyegzett RAM esemény. */
typedef struct
{
    int64_t epoch_s;
    app_event_level_t level;
    /* Növekvő sorszám: ebből derül ki, mi az új az SD-re mentés óta, és hogy
     * két mentés között kiesett-e bejegyzés a körkörös pufferből. */
    uint32_t serial;
    char text[APP_EVENT_TEXT_MAX];
    char text_en[APP_EVENT_TEXT_MAX];
} app_event_entry_t;

/* Körkörös RAM eseménynapló inicializálása. */
void app_events_init(void);

/* Formázott esemény hozzáadása a naplóhoz. */
void app_events_add(app_event_level_t level, const char *fmt, ...);

/* A legfrissebb események másolása a hívó által megadott tömbbe. */
size_t app_events_snapshot(app_event_entry_t *out, size_t max_items);

/* A legutóbb rögzített esemény sorszáma; nulla, ha még nincs bejegyzés.
 * Ebből a mentés másolás nélkül tudja, hány bejegyzés vár kiírásra. */
uint32_t app_events_latest_serial(void);

/*
 * A megadott sorszám utáni események másolása, legrégebbitől a legújabbig.
 * `dropped` a két hívás között a pufferből kiesett bejegyzések száma, így a
 * mentés jelezni tudja a hiányt. A visszaadott darabszám nulla is lehet.
 */
size_t app_events_since(uint32_t after, app_event_entry_t *out, size_t max_items,
                        uint32_t *dropped);
