#!/usr/bin/env python3
"""Exercise real time module and reject the lwIP timer value that rebooted P4."""
import os, re, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
delay = int(re.search(r'^CONFIG_LWIP_SNTP_UPDATE_DELAY=(\d+)$', (ROOT/'sdkconfig.defaults').read_text(), re.M).group(1))
assert 7*86400000 < delay <= (2**32-1)//4
with tempfile.TemporaryDirectory(prefix='zolee-time-') as directory:
    tmp = Path(directory)
    (tmp/'freertos').mkdir()
    # Match the real FreeRTOS macro: multiplication happens in TickType_t.
    (tmp/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <stdint.h>
#ifndef TEST_TICK_HZ
#define TEST_TICK_HZ 1000
#endif
#define configTICK_RATE_HZ TEST_TICK_HZ
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (((TickType_t)(ms)*(TickType_t)configTICK_RATE_HZ)/(TickType_t)1000U)
''')
    (tmp/'freertos/task.h').write_text((ROOT/'tests/stubs/freertos/task.h').read_text())
    (tmp/'sdkconfig.h').write_text(f'#define CONFIG_LWIP_SNTP_UPDATE_DELAY {delay}\n')
    (tmp/'esp_netif_sntp.h').write_text('''#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#define ESP_ERR_TIMEOUT 0x107
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) { .start=true, .wait_for_sync=true, .servers={server} }
typedef struct { bool start, wait_for_sync; const char *servers[1]; } esp_sntp_config_t;
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg);
esp_err_t esp_netif_sntp_start(void);
esp_err_t esp_netif_sntp_sync_wait(TickType_t timeout);
''')
    (tmp/'test.c').write_text('''#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include "sdkconfig.h"
#include "app_time.h"
#include "app_events.h"
#include "esp_netif_sntp.h"
static bool connected, synced;
static int starts, wait_result=ESP_ERR_TIMEOUT;
static void (*background_task)(void *);
static jmp_buf stop_task;
static unsigned task_creates, delays, stop_after, warnings, successes;
static TickType_t observed[8];
static bool reconnect_on_retry, succeed_on_retry;
bool app_wifi_is_connected(void) { return connected; }
void app_state_set_time_synced(bool value) { synced=value; }
void app_events_add(app_event_level_t level,const char *fmt,...) {
 (void)fmt;if(level==APP_EVENT_WARNING)++warnings;else ++successes;
}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned prio,void *handle) {
 background_task=fn;++task_creates;(void)name;(void)stack;(void)arg;(void)prio;(void)handle;return pdPASS;
}
void vTaskDelay(TickType_t ticks) {
 assert(delays<8);observed[delays++]=ticks;
 if(delays==stop_after)longjmp(stop_task,1);
 if(delays==2&&reconnect_on_retry)connected=true;
 if(delays==2&&succeed_on_retry)wait_result=ESP_OK;
}
static void run_background(unsigned count) {
 delays=0;stop_after=count;
 if(!setjmp(stop_task))background_task(NULL);
 assert(delays==count);
}
static TickType_t seconds_to_ticks(uint32_t seconds) {
 return (TickType_t)((uint64_t)seconds*configTICK_RATE_HZ);
}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg) {
 assert(!cfg->start && cfg->wait_for_sync);
 assert(strcmp(cfg->servers[0], "192.0.2.10")==0);return ESP_OK;
}
esp_err_t esp_netif_sntp_start(void) {
 /* The same unsigned bound enforced by lwIP sys_timeout. */
 assert(CONFIG_LWIP_SNTP_UPDATE_DELAY <= UINT32_MAX/4U);++starts;return ESP_OK;
}
esp_err_t esp_netif_sntp_sync_wait(TickType_t timeout) { assert(timeout==pdMS_TO_TICKS(10000));return wait_result; }
int main(void) {
 assert(app_time_init(NULL)==ESP_ERR_INVALID_ARG);
 assert(app_time_sync_once()==ESP_ERR_INVALID_STATE);
 app_config_t cfg={0};strcpy(cfg.time.ntp_server,"192.0.2.10");
 strcpy(cfg.time.timezone,"Europe/Budapest");cfg.time.auto_dst=true;
 cfg.time.resync_hours=6;cfg.time.background_retry_minutes=15;
#ifdef TEST_MAX_INTERVAL
 cfg.time.resync_hours=168;cfg.time.background_retry_minutes=1440;
#endif
 assert(app_time_init(&cfg)==ESP_OK);
 assert(app_time_init(&cfg)==ESP_ERR_INVALID_STATE);
 assert(app_time_sync_once()==ESP_ERR_INVALID_STATE && starts==0);
 connected=true;assert(app_time_sync_once()==ESP_ERR_TIMEOUT && !synced);
 wait_result=ESP_OK;assert(app_time_sync_once()==ESP_OK && synced && starts==2);
 assert(app_time_start_background_task()==ESP_OK);
 assert(app_time_start_background_task()==ESP_OK&&task_creates==1&&background_task);
 TickType_t normal=seconds_to_ticks(cfg.time.resync_hours*3600U);
 TickType_t retry=seconds_to_ticks(cfg.time.background_retry_minutes*60U);
 int before=starts;
 run_background(4);
 assert(starts==before+3&&successes==3&&warnings==0);
 for(unsigned i=0;i<4;i++)assert(observed[i]==normal);
 // A failed sync retries on the minute interval, then returns to hours.
 wait_result=ESP_ERR_TIMEOUT;succeed_on_retry=true;
 before=starts;run_background(3);
 assert(observed[0]==normal&&observed[1]==retry&&observed[2]==normal);
 assert(starts==before+2&&warnings==1&&successes==4);
 succeed_on_retry=false;connected=false;reconnect_on_retry=true;
 before=starts;run_background(3);
 assert(observed[0]==normal&&observed[1]==retry&&observed[2]==normal);
 assert(starts==before+1&&successes==5);
 return 0;
}
''')
    cc=os.environ.get('CC','cc')
    args=[cc,'-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
          '-fno-sanitize-recover=all','-I'+str(tmp),'-I'+str(ROOT/'tests/stubs'),'-I'+str(ROOT/'main')]
    for variant in ([], ['-DTEST_TICK_HZ=100'], ['-DTEST_MAX_INTERVAL']):
        subprocess.run([*args,*variant,str(ROOT/'main/app_time.c'),str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')],check=True)
    (tmp/'sdkconfig.h').write_text('#define CONFIG_LWIP_SNTP_UPDATE_DELAY 1209600000\n')
    rejected=subprocess.run([*args,'-c',str(ROOT/'main/app_time.c'),'-o',str(tmp/'old.o')],capture_output=True,text=True)
    assert rejected.returncode!=0 and 'SNTP update delay exceeds lwIP' in rejected.stderr
print('PASS: actual SNTP module accepts IPv4 server, bounded wait/retry, 6h/168h background schedule at 100/1000Hz, success/failure/offline recovery; old 14-day timer rejected at compile time')
