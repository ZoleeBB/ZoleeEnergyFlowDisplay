#!/usr/bin/env python3
"""A tényleges linker-wrapper korlátos duplex DMA-t kér, a BSP módosítása nélkül."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-i2s-') as directory:
    path = Path(directory)
    (path / 'driver').mkdir()
    (path / 'sdkconfig.h').write_text('#define CONFIG_BSP_I2S_NUM 0\n')
    (path / 'driver/i2s_std.h').write_text('''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *i2s_chan_handle_t;
typedef struct { int id, role; uint32_t dma_desc_num, dma_frame_num;
                bool auto_clear, auto_clear_before_cb, allow_pd; int intr_priority; } i2s_chan_config_t;
esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
''')
    (path / 'driver.c').write_text('''
#include "driver/i2s_std.h"
i2s_chan_config_t received;
int result;
esp_err_t i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    received = *config;
    if (result) return result;
    if (tx) *tx = (void *)1;
    if (rx) *rx = (void *)2;
    return ESP_OK;
}
''')
    (path / 'test.c').write_text('''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "driver/i2s_std.h"
extern i2s_chan_config_t received;
extern int result;
int main(void)
{
    i2s_chan_config_t config = {.id=0, .role=1, .dma_desc_num=6, .dma_frame_num=240,
        .auto_clear=true, .auto_clear_before_cb=true, .allow_pd=true, .intr_priority=3};
    i2s_chan_config_t original = config;
    i2s_chan_handle_t tx = NULL, rx = NULL;
    assert(i2s_new_channel(&config, &tx, &rx) == ESP_OK && tx && rx);
    assert(received.dma_desc_num == 3 && received.dma_frame_num == 160);
    assert(received.dma_desc_num * received.dma_frame_num * 4 * 2 == 3840);
    received.dma_desc_num = original.dma_desc_num;
    received.dma_frame_num = original.dma_frame_num;
    assert(!memcmp(&received, &original, sizeof(original)));
    assert(!memcmp(&config, &original, sizeof(original)));
    config.id = 1;
    assert(i2s_new_channel(&config, &tx, &rx) == ESP_OK && received.dma_desc_num == 6);
    config.id = 0;
    assert(i2s_new_channel(&config, &tx, NULL) == ESP_OK && received.dma_desc_num == 6);
    assert(i2s_new_channel(NULL, &tx, &rx) == ESP_ERR_INVALID_ARG);
    config.dma_desc_num = 2; config.dma_frame_num = 100;
    assert(i2s_new_channel(&config, &tx, &rx) == ESP_OK);
    assert(received.dma_desc_num == 2 && received.dma_frame_num == 100);
    result = ESP_ERR_NO_MEM;
    assert(i2s_new_channel(&config, &tx, &rx) == ESP_ERR_NO_MEM);
    return 0;
}
''')
    binary = str(path / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-I' + directory, '-Itests/stubs',
        'main/app_intercom_i2s.c', str(path / 'driver.c'), str(path / 'test.c'),
        '-Wl,--wrap=i2s_new_channel', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
print('PASS: actual linker wrapper bounds duplex DMA to 3840 bytes; preserves config, ports, simplex, smaller buffers and allocation errors')
