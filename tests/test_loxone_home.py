#!/usr/bin/env python3
"""A v3 konfigurációs modell valódi C-kódja, hibás bemenetekkel és memóriahibákkal."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="loxone-home-") as directory:
    binary = str(Path(directory) / "test_home")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Itests/stubs", "-Imain", "main/app_loxone_model.c", "main/app_loxone_home_model.c",
        "tests/test_loxone_home.c", "-l:libcjson.so.1", "-lm", "-Wl,--wrap=calloc",
        "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=15)
