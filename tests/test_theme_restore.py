#!/usr/bin/env python3
"""A kaputelefon utáni visszatérés policy és kritikus zárolási invariánsai."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="theme-restore-") as directory:
    binary = str(Path(directory) / "theme_restore")
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-fno-sanitize-recover=all",
        "-Imain", "tests/test_theme_restore.c", "-o", binary,
    ], check=True, cwd=ROOT)
    subprocess.run([binary], check=True, timeout=10)

templates = (ROOT / "main/app_templates.c").read_text(encoding="utf-8")
recovery = templates.split("bool app_templates_try_temporary_builtin(void)", 1)[1].split(
    "esp_err_t app_templates_select(", 1
)[0]
assert "xSemaphoreTake(lock, 0)" in recovery
assert "portMAX_DELAY" not in recovery
assert "save_selection" not in recovery
assert "STORAGE_TAKE" not in recovery
assert "temporary_builtin = true" in recovery
assert 'const char *active_selection = temporary_builtin ? "builtin" : selected;' in templates
assert 'bool need_storage = strcmp(active_selection, "builtin") != 0;' in templates
assert "if (need_storage && !STORAGE_TRY())" in templates
assert "if (need_storage) STORAGE_GIVE();" in templates

ui = (ROOT / "main/app_template_ui.inc").read_text(encoding="utf-8")
assert "app_templates_try_temporary_builtin()" in ui
assert 'app_templates_select("builtin")' not in ui
assert "app_theme_restore_tick(" in ui
assert "template_fallback_timeouts >= 5" in ui
assert "app_storage_try_lock()" in ui
assert "app_ota_try_begin_operation()" in ui
assert "app_ota_end_operation()" in ui

home = (ROOT / "main/app_loxone_home_ui.inc").read_text(encoding="utf-8")
assert "static bool home_restore_watchdog(void)" in home
assert "home_reset_theme();" in home
assert "home_restore_failed = s_cfg.energy_flow_disabled;" in home

intercom = (ROOT / "main/app_intercom_ui.inc").read_text(encoding="utf-8")
assert "intercom_cleanup_since = 0;" in intercom
assert "app_intercom_video_busy()" in intercom
assert "app_ota_try_begin_operation()" in intercom
assert "app_ota_end_operation()" in intercom
assert "app_storage_try_lock()" in intercom
assert "app_storage_unlock();" in intercom
assert "esp_restart();" in intercom
print("PASS: nonblocking, temporary recovery and guarded media shutdown invariants")
