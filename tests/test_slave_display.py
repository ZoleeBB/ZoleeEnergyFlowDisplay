#!/usr/bin/env python3
"""A közös ébresztési állapotgép valódi C-kódja, vezérelt monoton idővel."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="slave-display-") as directory:
    binary = str(Path(directory) / "test_display")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Imain", "main/app_slave_display.c", "tests/test_slave_display.c", "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
