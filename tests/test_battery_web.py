#!/usr/bin/env python3
"""A kiadott C webes kártya és szkript végponttól a nullázógombig."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='battery-web-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_battery_web.c', '-l:libcjson.so.1', '-o', str(binary)], cwd=ROOT, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, text=True, capture_output=True, check=True)
    print(result.stderr, end='')
    html = Path(directory) / 'page.html'
    html.write_text(result.stdout)
    subprocess.run(['node', 'tests/test_battery_web.js', str(html)], cwd=ROOT, check=True)
