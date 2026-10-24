#!/usr/bin/env python3
"""A kamera keretezője és a tényleges csengetésmodell sanitizerrel."""
import os
import base64
import json
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

    fixtures = json.loads((ROOT / 'tests/fixtures/intercom_jpeg.json').read_text())
    paths = []
    for name, encoded in fixtures.items():
        jpeg = base64.b64decode(encoded)
        # Öt érvényes APP15 szegmens, bennük szándékos EOI-bájtokkal.
        payload = b'x' * 65529 + b'\xff\xd9xx'
        segment = b'\xff\xef' + (len(payload) + 2).to_bytes(2, 'big') + payload
        path = Path(directory) / (name + '.jpg')
        path.write_bytes(jpeg[:2] + segment * 5 + jpeg[2:])
        paths.append(str(path))
    binary = str(Path(directory) / 'jpeg-files')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Imain',
        'tests/test_intercom_jpeg_files.c', 'main/app_intercom_stream.c', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, *paths], check=True, timeout=15,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
