#!/usr/bin/env python3
"""A tényleges Otthon-tároló tesztje, valós ideiglenes fájlrendszeren."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-home-store-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/loxone_stubs', '-Itests/stubs', '-Imain', f'-DAPP_SD_MOUNT_POINT="{directory}"',
        'tests/test_loxone_home_store.c', 'main/app_loxone_home_store.c',
        'main/app_loxone_home_model.c', 'main/app_loxone_model.c', 'main/app_loxone_store.c', '-lmbedcrypto', '-l:libcjson.so.1', '-lm',
        '-Wl,--wrap=rename', '-Wl,--wrap=fsync', '-o', binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=15)

with tempfile.TemporaryDirectory(prefix='loxone-home-http-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Wno-misleading-indentation', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_loxone_home_http.c', 'main/app_ringtone_wav.c',
        'main/app_loxone_home_model.c', 'main/app_loxone_model.c',
        'main/app_slave_presence.c', 'main/app_slave_display.c', '-l:libcjson.so.1', '-lm',
        '-o', binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=15)
