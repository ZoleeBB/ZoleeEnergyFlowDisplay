#!/usr/bin/env python3
"""A valódi árnyékolásmodell, konfiguráció és parancsok korlátos tesztje."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-shading-') as directory:
    binary = str(Path(directory) / 'test')
    common = [os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
              '-Werror=frame-larger-than=8192', '-Itests/stubs', '-Imain']
    subprocess.run(common + ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                             'tests/test_shading.c', 'main/app_loxone_model.c',
                             'main/app_loxone_home_model.c', 'main/app_loxone_home_runtime.c',
                             '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'tests/fixtures/loxone-shading.json')],
                   check=True, timeout=20)
    # A célon más az ABI; a modell új helyi kereteit itt külön is korlátozzuk.
    for optimization in ('-O0', '-O2'):
        output = Path(directory) / optimization[1:]
        output.mkdir()
        for source in ('app_loxone_model', 'app_loxone_home_runtime'):
            subprocess.run(common + [optimization, '-Werror=format-truncation', '-fstack-usage',
                                     '-c', f'main/{source}.c', '-o', str(output / f'{source}.o')],
                           cwd=ROOT, check=True)
        checked = 0
        for report in output.glob('*.su'):
            for line in report.read_text().splitlines():
                if not any(marker in line for marker in
                           (':lx_shading_', ':shading_', ':home_shading_', ':lx_home_shading_')):
                    continue
                _, size, qualifier = line.split('\t')
                assert int(size) <= 1536, line
                assert qualifier in ('static', 'dynamic,bounded'), line
                checked += 1
        assert checked, 'Hiányzó árnyékolás-veremadatok'
    print('PASS: árnyékolásmodell helyi keretei O0/O2 mellett legfeljebb 1536 bájt')
