#!/usr/bin/env python3
"""Web settings and OTA regression checks; syntax and sensitive data contract."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
web=(root/'main/app_web.c').read_text()
settings=(root/'main/app_settings_web.inc').read_text()
page=(root/'assets/web/settings.html').read_text()
js=(root/'assets/web/settings.js').read_text()
assert 'app_stats_save_now()' in web
assert 'settings_register(s_server)' in web
assert 'X-Zolee-Settings' in settings and 'keep_secret(' in settings
assert 'cJSON_AddNullToObject(wifi,"password")' in settings
assert 'cJSON_AddNullToObject(mqtt,"password")' in settings
assert 'cJSON_AddNullToObject(ota,"password")' in settings
assert 'app_config_load_file(tmp,&next,error,sizeof(error))' in settings
assert 'app_stats_update_config(&next.statistics)' in settings
assert 'fsync(fileno(f))' in settings
assert 'config.json.webbak' in settings
assert '/*SETTINGS_JS*/' in page
assert 'X-Zolee-Settings' in js
assert "statistics.save_interval_minutes" in js
for path in ['settings.js','template-manager.js']:
    subprocess.run(['node','--check',str(root/'assets/web'/path)],check=True)
for header in ['app_settings_page.h','app_template_page.h']:
    # ESP-IDF compiles with -Werror=trigraphs; test the generated literal too.
    source = (root/'main'/header).read_text()
    assert not any(sequence in source for sequence in ("??=", "??/", "??'", "??(", "??)", "??!", "??<", "??>", "??-"))
    subprocess.run(['cc','-x','c','-fsyntax-only','-std=gnu11','-Werror=trigraphs',str(root/'main'/header)],check=True)

print('PASS: settings web contract and Javascript syntax')
