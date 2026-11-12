#!/usr/bin/env python3
"""A világítási adatmodell, parancsok és látható lap valódi C-tesztje."""
import os
import subprocess
import tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-lighting-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                    'tests/test_lighting.c', 'main/app_loxone_model.c',
                    'main/app_loxone_home_model.c', 'main/app_loxone_home_runtime.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'tests/fixtures/loxone-lighting.json')], check=True, timeout=20)
