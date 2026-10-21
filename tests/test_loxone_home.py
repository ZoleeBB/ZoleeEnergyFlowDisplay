#!/usr/bin/env python3
"""A v3 konfigurációs modell valódi C-kódja, hibás bemenetekkel és memóriahibákkal."""
import os
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SAMPLE = ROOT / 'examples/loxone-home.sample.json'
profile = json.loads(SAMPLE.read_text(encoding='utf-8'))
icons = {icon['id'] for icon in json.loads((ROOT / 'assets/loxone-home/catalog.json').read_text())['icons']}
assert all(entry['icon'] in icons for entry in profile['consumers'] +
           [card for page in profile['pages'] for card in page['cards']])
with tempfile.TemporaryDirectory(prefix="loxone-home-") as directory:
    binary = str(Path(directory) / "test_home")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Itests/stubs", "-Imain", "main/app_loxone_model.c", "main/app_loxone_home_model.c",
        "tests/test_loxone_home.c", "-l:libcjson.so.1", "-lm", "-Wl,--wrap=calloc",
        "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary, str(SAMPLE)], check=True, timeout=15)

with tempfile.TemporaryDirectory(prefix="loxone-home-runtime-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Itests/stubs", "-Imain", "main/app_loxone_model.c", "main/app_loxone_home_runtime.c",
        "tests/test_loxone_home_runtime.c", "-l:libcjson.so.1", "-lm", "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=15)
