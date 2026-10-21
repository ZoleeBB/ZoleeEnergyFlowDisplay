#!/usr/bin/env python3
"""A jelenlétmodell és a valódi Slave-fényerőállapotgép közös UBSan-próbája."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="slave-presence-") as directory:
    binary = str(Path(directory) / "test_presence")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Imain", "main/app_slave_display.c", "main/app_slave_presence.c",
        "tests/test_slave_presence.c", "-lm", "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
