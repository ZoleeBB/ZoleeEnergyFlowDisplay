#!/usr/bin/env python3
"""Típusos Loxone-blokkok: tényleges C-parser és modell, memóriaellenőrzéssel."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-controls-') as directory:
    binary = str(Path(directory) / 'test')
    common = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
              '-Werror=frame-larger-than=4096', '-Itests/stubs', '-Imain']
    subprocess.run(common + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             'tests/test_controls.c', 'main/app_loxone_model.c',
                             'main/app_loxone_home_model.c', '-l:libcjson.so.1', '-lm', '-o', binary],
                   cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'tests/fixtures/loxone-controls.json')], check=True, timeout=20)
    service = str(Path(directory) / 'service')
    subprocess.run(common + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             'tests/test_controls_service.c', 'main/app_loxone_model.c',
                             'main/app_loxone_home_runtime.c', '-l:libcjson.so.1', '-lm', '-o', service],
                   cwd=ROOT, check=True)
    subprocess.run([service, str(ROOT / 'tests/fixtures/loxone-controls.json')], check=True, timeout=20)
    for optimization in ('-O0', '-O2'):
        target = Path(directory) / optimization[1:]
        target.mkdir()
        subprocess.run(common + [optimization, '-Werror=format-truncation', '-fstack-usage',
                                 '-c', 'main/app_loxone_model.c', '-o', str(target / 'model.o')],
                       cwd=ROOT, check=True)
        checked = 0
        for line in (target / 'model.su').read_text().splitlines():
            if ':lx_controls_' not in line and ':controls_' not in line:
                continue
            _, size, qualifier = line.split('\t')
            assert int(size) <= 1536, line
            assert qualifier in ('static', 'dynamic,bounded'), line
            checked += 1
        assert checked
    print('PASS: controls local stack frames <= 1536 bytes with O0/O2')
