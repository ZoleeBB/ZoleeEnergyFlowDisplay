#!/usr/bin/env python3
"""Drive the real boot-time storage reset against a temporary SD filesystem."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
base_settings = ("config.json", "loxone.json", "loxone-home.json", "templates/order.txt",
                 "templates/selection.txt")
removed = [p + suffix for p in base_settings for suffix in ("", ".rstmp", ".rsbak")] + [
    "config.json.webtmp", "config.json.webbak", "config.json.rscheck", "loxone.json.tmp",
    "loxone.json.bak", "loxone-home.json.tmp", "loxone-home.json.bak", "templates/order.tmp",
    "templates/order.bak", "templates/selection.tmp", "templates/selection.bak",
    "logs/events.log", "logs/events-prev.log", "stats/2024-02-29.json",
    "stats/2025-01-03.json.tmp", "stats/2026-12-31.JSON.BAK",
]
preserved = ["config-notes.json", "private-backup.json", "logs/installer.log",
             "stats/2026-11-18.csv", "stats/readme.txt", "stats/2025-02-29.json",
             "stats/2026-11-18.json.txt", "ringtones/doorbell.wav", "templates/my-theme/template.json",
             "templates/my-theme/selection.txt", "templates/order-of-scenes.txt",
             "templates/loxone-home/icons/leaf.png", "photos/family.jpg"]

with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "reset"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-Itests/factory_reset_stubs",
                    "-Itests/stubs", "-Imain", "tests/test_factory_reset_storage.c",
                    "main/app_factory_reset_storage.c", "-Wl,--wrap=unlink", "-o", binary],
                   cwd=ROOT, check=True)
    for fault in ("none", "sd_write", "flash_read", "flash", "small_flash", "missing_flash", "nvs_read",
                  "nvs_write", "nvs_erase", "nvs_commit", "sd_conflict", "sd_unmounted"):
        sd = Path(tmp) / fault
        if fault != "sd_unmounted":
            for name in removed + preserved:
                p = sd / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(name.encode())
        if fault == "sd_conflict":
            (sd / "config.json").unlink()
            (sd / "config.json").mkdir()
            (sd / "config.json/user.txt").write_bytes(b"User file")
        subprocess.run([binary, sd, fault], check=True)
        if fault == "sd_unmounted":
            assert not sd.exists()
            continue
        for name in preserved:
            assert (sd / name).read_bytes() == name.encode(), name
        if fault == "sd_conflict":
            assert (sd / "config.json/user.txt").read_bytes() == b"User file"
            continue
        assert not any((sd / name).exists() for name in removed)
    subprocess.run([binary, "--cardless", "none"], check=True)
print("PASS: factory reset erases only each named SD/NVS setting, logs, valid daily history and both complete backup slots; cardless, interrupted erase/retry and unrelated assets survive")
