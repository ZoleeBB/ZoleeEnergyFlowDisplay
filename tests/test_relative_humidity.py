#!/usr/bin/env python3
"""Valódi C adatút és a natív méretű ikonok erőforráskorlátai."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='relative-humidity-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Werror=frame-larger-than=8192', '-fsanitize=undefined,address',
                    '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                    'tests/test_relative_humidity.c', 'main/app_loxone_model.c',
                    'main/app_payload.c', 'main/app_ble_protocol.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)

for definition in (ROOT / 'templates').glob('*/template.json'):
    package = json.loads(definition.read_text())
    for mode in ('light', 'dark'):
        elements = package[mode]['elements']
        humidity = [e for e in elements if e.get('source') == 'humidity']
        wind = any(e.get('source') == 'wind' for e in elements)
        assert len(humidity) == (2 if wind else 0), definition
        assert len(elements) <= 96
        if not humidity:
            continue
        icon = next(e for e in humidity if e['type'] == 'image')
        image = (definition.parent / icon['file']).read_bytes()
        assert struct.unpack('>II', image[16:24]) == (icon['w'], icon['h']) == (30, 30)
        assert len(image) < 2048
image = (ROOT / 'main/assets/png/weather_humidity.png').read_bytes()
assert struct.unpack('>II', image[16:24]) == (28, 28) and len(image) < 2048
print('PASS: humidity only on weather/wind themes; native 28/30px icons, <2KiB PNG and unchanged 96-node ceiling')
