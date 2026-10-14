#!/usr/bin/env python3
"""Exercise the actual linker wrapper against an internal-heap allocation failure."""
from pathlib import Path
import os
import importlib.util
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
    subprocess.run(['ar','rcs',str(tmp/'libbsp.a'),str(objects[2])],check=True)
    link = [cc,*flags,str(objects[3]),str(tmp/'libmain.a'),str(tmp/'libbsp.a'),str(objects[1]),
            '-Wl,--wrap=esp_lv_adapter_register_display','-o',str(tmp/'test')]
    # IDF scans main before the later BSP references. Without an anchor it drops
    # the unreferenced wrapper; exercise this real archive ordering explicitly.
    missing = subprocess.run(link, capture_output=True, text=True)
    assert missing.returncode != 0 and '__wrap_esp_lv_adapter_register_display' in missing.stderr
    subprocess.run([*link,'-Wl,-u,__wrap_esp_lv_adapter_register_display'],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('PASS: actual BSP call wrapped, 80 KB internal allocation failure redirected to PSRAM; config and other interfaces preserved')

# Verify the second adapter-level allocation, not just the BSP profile override.
spec = importlib.util.spec_from_file_location('patch_adapter', ROOT/'tools/patch_display_adapter.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
original = 'case ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL: {\n    void *buf = display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false, false);\n}\nvoid *other = display_manager_alloc_draw_buffer(other_size, false, false);\n'
fixed = module.patch(original)
assert 'cfg->draw_buf_pixels * color_size, profile->use_psram, false' in fixed
assert 'other_size, false, false' in fixed
assert module.patch(fixed) == fixed
for invalid in ['', original.replace('false, false', 'true, true'), original + original]:
    try: module.patch(invalid)
    except ValueError: pass
    else: raise AssertionError('Unsupported or ambiguous vendor source accepted')
print('PASS: partial adapter allocation corrected; other allocators preserved; unknown source fails closed')

# Compile the adapter-level path before and after patching: the original ignores
# even use_psram=true, while the corrected triple-partial branch honours it.
allocation_source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL 4
static int buffer;
static void *display_manager_alloc_draw_buffer(size_t size, bool psram, bool dma) {
    (void)dma;
    return psram || size <= 32768 ? &buffer : NULL;
}
static int prepare(bool use_psram) {
    struct { size_t draw_buf_pixels; } runtime = { 40000 }, *cfg = &runtime;
    struct { bool use_psram; } rendering = { use_psram }, *profile = &rendering;
    (void)profile;
    size_t color_size = 2;
    switch (ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL) {
    case ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL: {
        void *buf = display_manager_alloc_draw_buffer(cfg->draw_buf_pixels * color_size, false, false);
        return buf != NULL;
    }
    default: return 0;
    }
}
int main(void) {
    assert(prepare(false) == 0);
    assert(prepare(true) == EXPECT_PSRAM_SUCCESS);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='zolee-adapter-') as directory:
    tmp = Path(directory)
    for name, text, expected in [('original', allocation_source, 0), ('fixed', module.patch(allocation_source), 1)]:
        src=tmp/(name+'.c');src.write_text(text)
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                        f'-DEXPECT_PSRAM_SUCCESS={expected}',str(src),'-o',str(tmp/name)],check=True)
        subprocess.run([str(tmp/name)],check=True)
print('PASS: compiled triple-partial allocator reproduces failure and uses PSRAM after correction')

# Adapter 0.6.x uses a two-argument allocator; 0.7.x adds the DMA flag.
legacy = original.replace('false, false', 'false')
legacy_fixed = module.patch(legacy)
assert 'cfg->draw_buf_pixels * color_size, profile->use_psram)' in legacy_fixed
assert 'other_size, false)' in legacy_fixed
assert module.patch(legacy_fixed) == legacy_fixed
print('PASS: adapter 0.6 two-argument and newer three-argument allocators both supported')
