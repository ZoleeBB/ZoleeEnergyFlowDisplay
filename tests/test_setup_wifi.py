#!/usr/bin/env python3
"""Exercise the first-boot network chooser and STA/IP event handler with a scripted radio."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zolee-setup-wifi-') as directory:
    build = Path(directory)
    # Csak az SDK belépési pontjait helyettesítjük. Ugyanaz a STA és választó
    # fordul be, mint a két firmware-be. Egy teszteset egy teljes eszközindítás.
    for name in ('esp_wifi.h', 'esp_netif.h', 'esp_check.h', 'esp_event.h', 'lwip/ip_addr.h'):
        header = build / name
        header.parent.mkdir(exist_ok=True)
        header.write_text('#include "platform.h"\n')
    for name, folder in (('esp_heap_caps.h', 'inverter_stubs'),
                         ('freertos/event_groups.h', 'inverter_mqtt_stubs'),
                         ('mbedtls/platform_util.h', 'audio_stubs')):
        header = build / name
        header.parent.mkdir(exist_ok=True)
        header.symlink_to(ROOT / 'tests' / folder / name)
    # A gazdagépen a napló nem formáz, de kiértékeli az argumentumait, ahogy a firmware is.
    (build / 'esp_log.h').write_text(
        '#pragma once\n#include <stdio.h>\n#define ESP_LOGI(tag,...) '
        'do { (void)(tag); if (0) fprintf(stderr,__VA_ARGS__); } while(0)\n'
        '#define ESP_LOGW ESP_LOGI\n#define ESP_LOGE ESP_LOGI\n'
        'static inline const char *esp_err_to_name(int e) { (void)e; return ""; }\n')
    binary = build / 'setup_wifi'
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-include', 'platform.h',
        '-I' + str(build), '-Itests/setup_wifi_stubs', '-Itests/stubs', '-Imain',
        'main/app_setup_wifi.c', 'main/app_wifi.c', 'tests/test_setup_wifi.c',
        '-o', str(binary)
    ], cwd=ROOT, check=True)
    for scenario in ('retry', 'open', 'saved', 'no-dhcp', 'legacy', 'crowded'):
        subprocess.run([binary, scenario], check=True, timeout=8)
print('PASS: Wi-Fi scan cleanup, crowded multi-page selection, duplicate APs, retry, open/UTF-8 passwords, DHCP '
      'gating and keyboard-free saved-profile boot')
