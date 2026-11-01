#!/usr/bin/env python3
"""A Loxone kijelző-szintű türelmi idő valós C policy-je és UI integrációja."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="loxone-display-grace-") as directory:
    binary = str(Path(directory) / "loxone_display_grace")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Imain", "tests/test_loxone_display_grace.c", "-o", binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)

ui = (ROOT / "main/app_ui.c").read_text(encoding="utf-8")
assert '#include "app_loxone_display_grace.h"' in ui
assert "app_loxone_display_grace_tick(" in ui
assert "if (loxone_display_grace_update(&rt, have, now))" in ui
assert "if (loxone_display_is_held())" in ui
# A runtime sorrend: intercom -> OTA -> 10 s befagyasztás -> SD/home/tema.
runtime = ui.split("static void runtime(void *arg)", 1)[1]
assert runtime.index("intercom_prepare()") < runtime.index("app_ota_operation_active()")
assert runtime.index("app_ota_operation_active()") < runtime.index(
    "if (loxone_display_grace_update(&rt, have, now))")
assert runtime.index("if (loxone_display_grace_update(&rt, have, now))") < runtime.index(
    "home_prepare(")
assert runtime.index("if (loxone_display_grace_update(&rt, have, now))") < runtime.index(
    "template_loading_frame(")

home = (ROOT / "main/app_loxone_home_ui.inc").read_text(encoding="utf-8")
for name in ("home_action", "home_gesture", "home_open_clicked", "home_close"):
    body = home.split(f"static void {name}(", 1)[1].split("}", 1)[0]
    assert "loxone_display_is_held()" in body, name
theme = (ROOT / "main/app_template_ui.inc").read_text(encoding="utf-8")
assert "if (loxone_display_is_held()) return;" in theme
assert "APP_LOXONE_DISPLAY_GRACE_US" in (
    ROOT / "main/app_loxone_display_grace.h").read_text(encoding="utf-8")
print("PASS: Loxone display grace UI integration and noninteractive frozen home")
