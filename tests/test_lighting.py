#!/usr/bin/env python3
"""A világítási adatmodell, parancsok és látható lap valódi C-tesztje."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(ROOT / 'tests/test_lighting_stack.py')], check=True)
with tempfile.TemporaryDirectory(prefix='loxone-lighting-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                    'tests/test_lighting.c', 'main/app_loxone_model.c',
                    'main/app_loxone_home_model.c', 'main/app_loxone_home_runtime.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'tests/fixtures/loxone-lighting.json')], check=True, timeout=20)
    for test in ('test_lighting_bounds', 'test_home_values_stack'):
        bounds = str(Path(directory) / test)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                        f'tests/{test}.c', 'main/app_loxone_model.c',
                        'main/app_loxone_home_model.c', 'main/app_loxone_home_runtime.c',
                        '-l:libcjson.so.1', '-lm', '-o', bounds], cwd=ROOT, check=True)
        subprocess.run([bounds], check=True, timeout=20)
    # A sanitizer saját veremköltsége nem firmware-mérés. Külön, valódi GCC
    # fordítás készül optimalizálás nélkül és a céleszközével azonos O2-vel.
    for optimization in ('-O0', '-O2'):
        target = Path(directory) / optimization[1:]
        target.mkdir()
        for unit in ('app_loxone_model', 'app_loxone_home_runtime'):
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', optimization,
                            '-Wall', '-Wextra', '-Werror', '-Werror=format-truncation',
                            '-Werror=frame-larger-than=8192', '-fstack-usage',
                            '-Itests/stubs', '-Imain', '-c', f'main/{unit}.c',
                            '-o', str(target / f'{unit}.o')], cwd=ROOT, check=True)
        subprocess.run([sys.executable, str(ROOT / 'tools/check_lighting_stack.py'),
                        '--build-dir', str(target), '--model-only'], check=True)
