#!/usr/bin/env python3
"""Exercise the actual linker wrapper against an internal-heap allocation failure."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zolee-display-') as directory:
    tmp = Path(directory)
    (tmp/'esp_lv_adapter_display.h').write_text('''
#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct { int value; } lv_display_t;
enum { ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI = 1, ESP_LV_ADAPTER_PANEL_IF_OTHER = 2 };
typedef struct {
    void *panel, *panel_io;
    struct { int interface, rotation, hor_res, ver_res, buffer_height;
             bool use_psram, enable_ppa_accel, require_double_buffer; } profile;
    int tear_avoid_mode;
} esp_lv_adapter_display_config_t;
lv_display_t *esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);
''')
    (tmp/'adapter.c').write_text('''
#include "esp_lv_adapter_display.h"
lv_display_t screen;
esp_lv_adapter_display_config_t registered;
int calls;
lv_display_t *esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config) {
    ++calls;
    if (!config) return NULL;
    registered = *config;
    size_t bytes = (size_t)config->profile.hor_res * config->profile.buffer_height * 2;
    /* Simulate the reported 80 KB request with only 32 KB internal available. */
    return config->profile.use_psram || bytes <= 32768 ? &screen : NULL;
}
''')
    (tmp/'bsp.c').write_text('''
#include "esp_lv_adapter_display.h"
lv_display_t *bsp_start(const esp_lv_adapter_display_config_t *config) {
    return esp_lv_adapter_register_display(config);
}
''')
    (tmp/'test.c').write_text('''
#include <assert.h>
#include <string.h>
#include "esp_lv_adapter_display.h"
extern esp_lv_adapter_display_config_t registered;
extern int calls;
lv_display_t *bsp_start(const esp_lv_adapter_display_config_t *config);
lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);
int main(void) {
    esp_lv_adapter_display_config_t config = {
        .panel=(void *)1, .panel_io=(void *)2,
        .profile={.interface=ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI, .rotation=90,
                  .hor_res=800, .ver_res=1280, .buffer_height=50}, .tear_avoid_mode=4
    };
    esp_lv_adapter_display_config_t original = config;
    assert(__real_esp_lv_adapter_register_display(&config) == NULL);
    assert(bsp_start(&config) != NULL);
    assert(registered.profile.use_psram);
    registered.profile.use_psram = false;
    assert(memcmp(&registered, &original, sizeof(original)) == 0);
    assert(memcmp(&config, &original, sizeof(original)) == 0);
    config.profile.interface=ESP_LV_ADAPTER_PANEL_IF_OTHER;
    assert(bsp_start(&config) == NULL);
    assert(!registered.profile.use_psram);
    config.profile.use_psram=true;
    assert(bsp_start(&config) != NULL);
    assert(bsp_start(NULL) == NULL);
    assert(calls == 5);
    return 0;
}
''')
    cc = os.environ.get('CC', 'cc')
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
             '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I'+str(tmp),
             '-I'+str(ROOT/'tests/stubs')]
    objects = []
    for source in [ROOT/'main/app_display.c', tmp/'adapter.c', tmp/'bsp.c', tmp/'test.c']:
        obj=tmp/(source.stem+'.o'); objects.append(obj)
        subprocess.run([cc,*flags,'-c',str(source),'-o',str(obj)],check=True)
    # Archive linking mirrors IDF; the wrapper must be pulled from the main archive.
    subprocess.run(['ar','rcs',str(tmp/'libmain.a'),str(objects[0])],check=True)
    subprocess.run([cc,*flags,str(objects[3]),str(objects[2]),str(tmp/'libmain.a'),str(objects[1]),
                    '-Wl,--wrap=esp_lv_adapter_register_display','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: actual BSP call wrapped, 80 KB internal allocation failure redirected to PSRAM; config and other interfaces preserved')
