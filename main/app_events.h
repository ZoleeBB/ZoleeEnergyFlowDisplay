#pragma once
#include <stddef.h>
#include <stdint.h>
#define APP_EVENT_LOG_CAPACITY 12
#define APP_EVENT_TEXT_MAX 128
typedef enum { APP_EVENT_INFO=0, APP_EVENT_WARNING, APP_EVENT_ERROR } app_event_level_t;
typedef struct { int64_t epoch_s; app_event_level_t level; char text[APP_EVENT_TEXT_MAX]; } app_event_entry_t;
void app_events_init(void);
void app_events_add(app_event_level_t level, const char *fmt, ...);
size_t app_events_snapshot(app_event_entry_t *out, size_t max_items);
