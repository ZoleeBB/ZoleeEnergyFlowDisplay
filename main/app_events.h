#pragma once

#include <stddef.h>
#include <stdint.h>

/* A RAM eseménypufferben megtartott bejegyzések száma. */
#define APP_EVENT_LOG_CAPACITY 12

/* Egy esemény maximális szöveghossza a lezáró nulla nélkül. */
#define APP_EVENT_TEXT_MAX 128

/* Esemény súlyossági szintek. */
typedef enum {
    APP_EVENT_INFO = 0,
    APP_EVENT_WARNING,
    APP_EVENT_ERROR
} app_event_level_t;

/* Egy időbélyegzett RAM esemény. */
typedef struct {
    int64_t epoch_s;
    app_event_level_t level;
    char text[APP_EVENT_TEXT_MAX];
} app_event_entry_t;

/* Körkörös RAM eseménynapló inicializálása. */
void app_events_init(void);

/* Formázott esemény hozzáadása a naplóhoz. */
void app_events_add(app_event_level_t level, const char *fmt, ...);

/* A legfrissebb események másolása a hívó által megadott tömbbe. */
size_t app_events_snapshot(app_event_entry_t *out, size_t max_items);
