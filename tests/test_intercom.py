#!/usr/bin/env python3
"""A kamera keretezője és a tényleges csengetésmodell sanitizerrel."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_intercom.c', 'main/app_intercom_stream.c',
        'main/app_loxone_home_runtime.c', 'main/app_loxone_model.c', 'main/app_loxone_home_model.c',
        '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=15, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
