#!/usr/bin/env python3
"""A valódi háttérmentő és GATT-kiszolgáló, NVS/RTOS hibainjektálással."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="presence-service-") as directory:
    binary = str(Path(directory) / "test_presence_service")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-Werror=frame-larger-than=8192", "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Itests/ble_profile_stubs", "-Itests/stubs", "-Imain",
        "tests/test_slave_presence_service.c", "main/app_slave_presence.c",
        "main/app_slave_display.c", "-lm", "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
# A tényleges GATT-leíró titkosítást, MITM-et és 16 bájtos kulcsot kér.
seed = (ROOT / "main/app_ble_seed.c").read_text()
section = seed.split(".uuid = &s_presence_uuid.u,", 1)[1].split("},", 1)[0]
assert "BLE_GATT_CHR_F_READ_ENC" in section
assert "BLE_GATT_CHR_F_READ_AUTHEN" in section and ".min_key_size = 16" in section
assert "BLE_GATT_CHR_F_WRITE" not in section
