#include "app_events.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static app_event_entry_t s_entries[APP_EVENT_LOG_CAPACITY];
static size_t s_count, s_next;

void app_events_init(void){ if(!s_mutex)s_mutex=xSemaphoreCreateMutex(); s_count=0; s_next=0; memset(s_entries,0,sizeof(s_entries)); }
void app_events_add(app_event_level_t level,const char *fmt,...){
    if(!s_mutex||!fmt)return;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    app_event_entry_t *e=&s_entries[s_next]; memset(e,0,sizeof(*e)); e->level=level; e->epoch_s=(int64_t)time(NULL);
    va_list ap; va_start(ap,fmt); vsnprintf(e->text,sizeof(e->text),fmt,ap); va_end(ap);
    s_next=(s_next+1)%APP_EVENT_LOG_CAPACITY; if(s_count<APP_EVENT_LOG_CAPACITY)s_count++;
    xSemaphoreGive(s_mutex);
}
size_t app_events_snapshot(app_event_entry_t *out,size_t max_items){
    if(!out||!max_items||!s_mutex)return 0;
    xSemaphoreTake(s_mutex,portMAX_DELAY);
    size_t n=s_count<max_items?s_count:max_items;
    size_t start=(s_next+APP_EVENT_LOG_CAPACITY-s_count)%APP_EVENT_LOG_CAPACITY;
    size_t skip=s_count>n?s_count-n:0; start=(start+skip)%APP_EVENT_LOG_CAPACITY;
    for(size_t i=0;i<n;i++)out[i]=s_entries[(start+i)%APP_EVENT_LOG_CAPACITY];
    xSemaphoreGive(s_mutex); return n;
}
