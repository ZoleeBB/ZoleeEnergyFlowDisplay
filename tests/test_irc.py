#!/usr/bin/env python3
"""A valódi IRC-modell és parancsok memória-/veremellenőrzése."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-irc-') as directory:
    binary = str(Path(directory) / 'test')
    common = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
              '-Werror=frame-larger-than=8192', '-Itests/stubs', '-Imain']
    subprocess.run(common + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             'tests/test_irc.c', 'main/app_loxone_model.c',
                             'main/app_loxone_home_model.c', 'main/app_loxone_home_runtime.c',
                             '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'tests/fixtures/loxone-irc.json')], check=True, timeout=20)
    # Az ASan-verem nem céleszközmérés; a modell saját keretét külön ellenőrizzük.
    for optimization in ('-O0', '-O2'):
        output = Path(directory) / optimization[1:]
        output.mkdir()
        subprocess.run(common + [optimization, '-Werror=format-truncation', '-fstack-usage',
                                 '-c', 'main/app_loxone_model.c', '-o', str(output / 'model.o')],
                       cwd=ROOT, check=True)
        checked = 0
        for line in (output / 'model.su').read_text().splitlines():
            if ':lx_irc_' not in line and ':irc_' not in line:
                continue
            _, size, qualifier = line.split('\t')
            assert int(size) <= 1536, line
            assert qualifier in ('static', 'dynamic,bounded'), line
            checked += 1
        assert checked, 'Hiányzó IRC-veremadatok'
    print('PASS: IRC modell helyi keretei O0/O2 mellett legfeljebb 1536 bájt')
