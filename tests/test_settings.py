#!/usr/bin/env python3
"""Web settings and OTA regression checks; syntax and sensitive data contract."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
web=(root/'main/app_web.c').read_text()
settings=(root/'main/app_settings_web.inc').read_text()
page=(root/'assets/web/settings.html').read_text()
js=(root/'assets/web/settings.js').read_text()
assert 'app_stats_save_now()' in web
assert 'settings_register(s_server)' in web
assert '.handler = app_boot_config_setup_pending() ? settings_page : root_handler' in web
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
assert "display.rotation_degrees" in js
assert "csatlakozók jobb oldalon" in js and "csatlakozók bal oldalon" in js
assert 'NUM(display,"rotation_degrees",v->display.rotation_degrees)' in settings
for path in ['settings.js','template-manager.js']:
    subprocess.run(['node','--check',str(root/'assets/web'/path)],check=True)
for header in ['app_settings_page.h','app_template_page.h']:
    # ESP-IDF compiles with -Werror=trigraphs; test the generated literal too.
    source = (root/'main'/header).read_text()
    assert not any(sequence in source for sequence in ("??=", "??/", "??'", "??(", "??)", "??!", "??<", "??>", "??-"))
    subprocess.run(['cc','-x','c','-fsyntax-only','-std=gnu11','-Werror=trigraphs',str(root/'main'/header)],check=True)

# A gazdagépen pontosan a firmware JSON- és HTTP-válaszfüggvényei futnak.
with tempfile.TemporaryDirectory(prefix='settings-memory-') as folder:
    work = Path(folder)
    functions = []
    for name in ('static cJSON *settings_json(', 'static esp_err_t settings_memory_error(',
                 'static esp_err_t settings_response(', 'static esp_err_t settings_get(', 'static bool keep_secret('):
        start = settings.index(name)
        functions.append(settings[start:settings.index('\n}', start) + 2])
    (work/'settings_functions.inc').write_text('\n'.join(functions))
    functions = []
    for name in ('static const char *event_level_text(', 'static esp_err_t sendf(',
                 'static void html_escape(', 'static esp_err_t send_recent_events('):
        start = web.index(name)
        functions.append(web[start:web.index('\n}', start) + 2])
    (work/'event_functions.inc').write_text('\n'.join(functions))
    binary = work/'settings-memory'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                    '-I' + folder, 'tests/test_admin_memory.c', '-l:libcjson.so.1', '-o', binary],
                   cwd=root, check=True)
    subprocess.run([binary], check=True)

print('PASS: settings web contract and Javascript syntax')
