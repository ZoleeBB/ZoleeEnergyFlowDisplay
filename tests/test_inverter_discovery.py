"""A kereső állapotgépe és a helyi protokollokra korlátozott címkiválasztás."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    exe = Path(temp) / "discovery"
    wrappers = ["socket", "fcntl", "select", "bind", "connect", "close", "setsockopt", "sendto", "recvfrom"]
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=768", "-fsanitize=undefined", "-DESP_ERR_NOT_SUPPORTED=0x106",
        "-Imain", "-Itests/discovery_stubs", "-Itests/inverter_stubs", "-Itests/stubs",
        "main/app_inverter_discovery.c", "tests/test_inverter_discovery.c", "-pthread",
        *["-Wl,--wrap=" + name for name in wrappers], "-o", exe
    ], cwd=ROOT, check=True)
    subprocess.run([exe], cwd=ROOT, check=True, timeout=20)
