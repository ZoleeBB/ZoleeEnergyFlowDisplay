#!/usr/bin/env python3
"""Run actual C modules with mocked time, RTOS locks and SD fault injection."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_relative_humidity.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_workflows.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_ci_plan.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_documentation.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_theme_restore.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_loxone_display_grace.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_support_web.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_battery_web.py')], check=True)
with tempfile.TemporaryDirectory(prefix='energy-flow-disabled-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
                    'tests/test_energy_flow_disabled.c', 'main/app_config.c', 'main/app_ble_protocol.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='energy-assistant-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_energy_assistant.c', 'main/app_energy_assistant.c',
        '-lm', '-o', binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='battery-diagnostics-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_battery_diagnostics.c',
        'main/app_battery_diagnostics.c', '-l:libcjson.so.1', '-lm', '-o', binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='template-raw-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([
        os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', 'tests/test_template_raw.c', 'main/app_template_raw.c',
        '-lz', '-lm', '-o', binary,
    ], cwd=ROOT, check=True)
    subprocess.run([binary, str(ROOT / 'templates/05-calm-home/day.png'),
                    str(ROOT / 'templates/09-solar-journey/day.png'),
                    str(ROOT / 'main/assets/png/flow_solar.png')],
                   check=True, cwd=directory, timeout=120)

with tempfile.TemporaryDirectory(prefix='zolee-backup-') as directory:
    binary = str(Path(directory) / 'test_backup')
    sd = Path(directory) / 'sd'
    sd.mkdir()
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Werror=frame-larger-than=8192', '-fsanitize=undefined,address',
                    '-fno-sanitize-recover=all', '-Itests/ble_profile_stubs', '-Itests/stubs', '-Imain',
                    'tests/test_backup.c', 'main/app_backup.c', 'main/app_config.c',
                    'main/app_loxone_model.c', 'main/app_loxone_home_model.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, str(sd), 'examples/config.loxone.sample.json',
                    'examples/loxone-home.sample.json'], cwd=ROOT, check=True, timeout=20)
# A mentett napló megjelenítési sorrendje (a legfrissebb elöl).
with tempfile.TemporaryDirectory(prefix='zolee-event-view-') as directory:
    binary = str(Path(directory) / 'test_event_log_view')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Imain',
                    'tests/test_event_log_view.c', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=20)

# Az eseménynapló SD-re mentése: kötegelés, foglalt kártya, napváltás, korlátok.
with tempfile.TemporaryDirectory(prefix='zolee-event-store-') as directory:
    logs = Path(directory) / 'logs'
    logs.mkdir()
    binary = str(Path(directory) / 'test_event_store')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined,address', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain',
                    '-DAPP_EVENT_STORE_DIR="' + str(logs) + '"',
                    '-DAPP_EVENT_STORE_MAX_BYTES=8192',
                    'tests/test_event_store.c', 'main/app_events.c', 'main/app_event_store.c',
                    '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=30)
    # Újraindítás szimulálása: ugyanazok a fájlok, friss modulállapot.
    subprocess.run([binary, 'resume'], check=True, timeout=30)
    # A kiírás a 3-4 KiB-os karbantartó feladat vermén fut: egyetlen nagy
    # tömb is bootciklust okozott, ezért a keretméret ellenőrzött.
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Werror=frame-larger-than=768', '-Itests/stubs', '-Imain',
                    '-DAPP_EVENT_STORE_DIR="' + str(logs) + '"',
                    '-c', 'main/app_event_store.c', '-o', str(Path(directory) / 'frame.o')],
                   cwd=ROOT, check=True)

with tempfile.TemporaryDirectory(prefix='zolee-template-remote-') as directory:
    binary = str(Path(directory) / 'test_template_remote')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-fsanitize=undefined,address',
                    '-fno-sanitize-recover=all', '-DAPP_TEMPLATE_DESKTOP', '-Isimulator/compat',
                    '-Itests/stubs', '-Imain', 'tests/test_template_remote.c',
                    'main/app_template_remote.c', 'main/app_templates.c', 'main/app_template_raw.c', 'main/app_template_zip.c',
                    'main/app_ringtone_wav.c', 'main/app_ota_release.c', '-lz', '-lm',
                    '-l:libcjson.so.1', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary, 'templates/10-battery-diagnostics/template.json'], cwd=ROOT,
                   check=True, timeout=20)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_slave_display.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_slave_presence.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_slave_presence_service.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_loxone_home.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_lighting.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_irc.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_shading.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_controls.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_audio.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_audio_stack.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_audio_transport.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_intercom.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_intercom_audio.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_intercom_distributed.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_ringtones.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_loxone_home_store.py')], check=True)
subprocess.run(['node', str(ROOT / 'tests/test_loxone_home_web.js')], cwd=ROOT, check=True)
subprocess.run(['node', str(ROOT / 'tests/test_slave_presence_web.js')], cwd=ROOT, check=True)
with tempfile.TemporaryDirectory(prefix='zolee-ble-stats-transfer-') as directory:
    binary = str(Path(directory) / 'test_ble_stats_transfer')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ble_stats_transfer.c',
                    'main/app_ble_stats_protocol.c', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-boot-config-') as directory:
    binary = str(Path(directory) / 'test_boot_config')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/ble_profile_stubs', '-Itests/stubs', '-Imain',
                    'tests/test_boot_config.c', 'main/app_boot_config.c', 'main/app_config.c',
                    '-l:libcjson.so.1', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ble-stats-') as directory:
    binary = str(Path(directory) / 'test_ble_stats')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ble_stats.c',
                    'main/app_ble_stats_protocol.c', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ble-profile-') as directory:
    binary = str(Path(directory) / 'test_ble_profile')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/ble_profile_stubs', '-Itests/ota_stubs', '-Itests/stubs', '-Imain',
                    'tests/test_ble_profile.c', 'main/app_ble_profile_model.c', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ble-security-') as directory:
    binary = str(Path(directory) / 'test_ble_security')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ble_security.c',
                    'main/app_ble_profile_model.c', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-auto-ota-') as directory:
    binary = str(Path(directory) / 'test_ota_auto')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/ota_stubs', '-Itests/stubs', '-Imain', 'tests/test_ota_auto.c',
                    'main/app_ota_release.c', '-l:libcjson.so.1', '-lmbedcrypto', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ota-release-') as directory:
    binary = str(Path(directory) / 'test_ota_release')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ota_release.c',
                    'main/app_ota_release.c', '-l:libcjson.so.1', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
# Ugyanaz a release-választó a 4B névterével is fusson; más hardver nem tölthető le.
with tempfile.TemporaryDirectory(prefix='zolee-slave-ota-release-') as directory:
    binary = str(Path(directory) / 'test_ota_release')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-DAPP_OTA_ASSET_PREFIX="ZoleeEnergyFlowDisplay-slave-4b"',
                    '-Itests/stubs', '-Imain', 'tests/test_ota_release.c',
                    'main/app_ota_release.c', '-l:libcjson.so.1', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ble-') as directory:
    binary = str(Path(directory) / 'test_ble_protocol')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ble_protocol.c',
                    'main/app_ble_protocol.c', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-ble-slave-') as directory:
    for flags in ([], ['-DAPP_INTERCOM_SLAVE']):
        binary = str(Path(directory) / 'test_ble_slave')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        *flags, '-Itests/stubs', '-Imain', 'tests/test_ble_slave.c',
                        'main/app_ble_protocol.c', 'main/app_ble_stats_protocol.c',
                        'main/app_slave_presence.c', 'main/app_slave_display.c', '-lm', '-o', binary], cwd=ROOT, check=True)
        subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-tests-') as directory:
    temp = Path(directory)
    sources = ['tests/test_core.c', 'main/app_payload.c', 'main/app_config.c',
               'main/app_http.c', 'main/app_ingest.c', 'main/app_stats.c', 'main/app_battery_diagnostics.c', 'main/app_events.c', 'main/app_state.c']
    command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
               '-Werror', '-Werror=frame-larger-than=8192', '-fsanitize=undefined',
               '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain', '-Dtime=host_time',
               f'-DAPP_SD_MOUNT_POINT="{temp}"', *sources, '-lm', '-pthread',
               '-l:libcjson.so.1', '-Wl,--wrap=fsync', '-Wl,--wrap=rename', '-o', str(temp/'test_core')]
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(temp/'test_core')], check=True, timeout=30)
# A forgatás és az érintési leképezés regresziós ellenőrzése.
with tempfile.TemporaryDirectory(prefix='zolee-orientation-') as directory:
    binary=str(Path(directory)/'test_display_orientation')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra',
                    '-Werror','-Imain','tests/test_display_orientation.c','-o',binary],cwd=ROOT,check=True)
    subprocess.run([binary],check=True,timeout=10)
main_source=(ROOT/'main/app_main.c').read_text()
assert main_source.index('app_ui_init(s_active_rotation)') < main_source.index('app_storage_boot_mount()') < main_source.index('app_config_load_file(')
assert 'app_display_boot_sync(' in main_source
# A 90 fokos próba/letiltás védelmet eltávolítottuk: indulás közbeni újraindítás
# mindig visszaállította 270 fokra, pedig a 90 fokos mód stabil.
assert 'orientation_verified_task' not in main_source and 'blocked' not in main_source
# A Windows-szimulátor a POSIX olvasási úttól külön ágon fordul. A fejlécben
# meghirdetett függvények definíciója ezért nem állhat `_WIN32`-függő blokkban:
# ott az MSVC-s build linkelésnél bukik (LNK2019: unresolved external symbol).
# Az információs oldal és a mentett napló is a legfrissebb bejegyzéssel kezd,
# ahogy a kijelző diagnosztikai nézete.
web_source=(ROOT/'main/app_web.c').read_text()
assert 'size_t i = event_count - 1 - shown;' in web_source
assert 'app_event_log_reverse_lines(' in web_source
diagnostics_source=(ROOT/'main/app_template_ui.inc').read_text()
assert 's_diagnostic_events[s_diagnostic_event_count - 1 - i]' in diagnostics_source

templates_header=(ROOT/'main/app_templates.h').read_text()
exported={name for name in re.findall(r'^[A-Za-z_][\w ]*\**\s*(app_templates_\w+)\s*\(', templates_header, re.M)}
assert 'app_templates_sd_write' in exported
# Ugyanez a csapda fájlszintű statikus változóra is: a `_WIN32`-blokkban
# deklarált változót a blokkon kívüli kód nem használhatja (MSVC: C2065).
templates_source=(ROOT/'main/app_templates.c').read_text()
win32_declared=set()
inside=[]
for source_line in templates_source.splitlines():
    text=source_line.strip()
    if text.startswith('#if'):
        inside.append('_WIN32' in text)
    elif text.startswith('#elif'):
        if inside:
            inside[-1]=inside[-1] or '_WIN32' in text
    elif text.startswith('#endif'):
        if inside:
            inside.pop()
    elif any(inside):
        # Csak fájlszintű (behúzás nélküli) deklaráció: a függvényen belüli
        # statikus változó amúgy sem látszik kívülről.
        declared=re.match(r'^static\s+(?:const\s+|volatile\s+|unsigned\s+|signed\s+)*\w+\s+\**(\w+)\s*(?:=[^;]*)?;$',source_line)
        if declared:
            win32_declared.add(declared.group(1))
inside=[]
for source_line in templates_source.splitlines():
    text=source_line.strip()
    if text.startswith('#if'):
        inside.append('_WIN32' in text)
    elif text.startswith('#elif'):
        if inside:
            inside[-1]=inside[-1] or '_WIN32' in text
    elif text.startswith('#endif'):
        if inside:
            inside.pop()
    elif not any(inside):
        for name in win32_declared:
            assert not re.search(r'\b'+re.escape(name)+r'\b',text), \
                f'{name} a _WIN32-blokkban deklaralt, de kivul is hasznaljak'

win32_stack=[]
for source_line in templates_source.splitlines():
    text=source_line.strip()
    if text.startswith('#if'):
        win32_stack.append('_WIN32' in text)
    elif text.startswith('#elif'):
        if win32_stack:
            win32_stack[-1]=win32_stack[-1] or '_WIN32' in text
    elif text.startswith('#endif'):
        if win32_stack:
            win32_stack.pop()
    elif not text.startswith('static'):
        found=re.match(r'^[A-Za-z_][\w ]*\**\s*(app_templates_\w+)\s*\(', text)
        if found and found.group(1) in exported and text.endswith(')'):
            assert not any(win32_stack), f'{found.group(1)} definicioja _WIN32-fuggo blokkban van'
ui_source=(ROOT/'main/app_ui.c').read_text()
assert 'lv_display_set_rotation(s_panel_display' not in ui_source
assert 'ESP_LV_ADAPTER_ROTATE_90' in ui_source and 'ESP_LV_ADAPTER_ROTATE_270' in ui_source
with tempfile.TemporaryDirectory(prefix='zolee-boot-') as directory:
    binary=str(Path(directory)/'test_display_boot')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                    '-Imain','tests/test_display_boot.c','-o',binary],cwd=ROOT,check=True)
    subprocess.run([binary],check=True,timeout=10)
# Generated full/partial five-minute energy histories, built as real C code.
with tempfile.TemporaryDirectory(prefix="zolee-compare-") as directory:
    binary=str(Path(directory)/"test_stats_comparison")
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O2","-Wall","-Wextra",
                    "-Werror","-Imain","tests/test_stats_comparison.c","-lm",
                    "-o",binary],cwd=ROOT,check=True)
    subprocess.run([binary],check=True,timeout=10)
# Run the complete HTTP sample with stubbed Loxone I/O, including unchanged inputs.
subprocess.run([os.sys.executable, str(ROOT/"tests/test_http_picoc.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_release.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_auto_release.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_templates.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_home_resources.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/'tests/test_web_preview.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT/'tests/test_settings.py')], check=True)
subprocess.run(['node', str(ROOT/'tests/test_settings_web.js')], cwd=ROOT, check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_memory.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_rotation.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_partial.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_time.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_hosted_rx.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_sdio_window.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_runtime_resources.py")], check=True)

# Kritikus vizuális és animációs regressziók.
subprocess.run([os.sys.executable, str(ROOT/'tests/test_visual_regressions.py')], check=True)

# Loxone bináris adatfolyam és párosítás valódi C-kóddal.
with tempfile.TemporaryDirectory(prefix='zolee-loxone-') as directory:
    binary=str(Path(directory)/'test_loxone_model')
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-Itests/stubs','-Imain',
                    'main/app_loxone_model.c','main/app_payload.c','tests/test_loxone_model.c',
                    '-l:libcjson.so.1','-lm','-o',binary],cwd=ROOT,check=True)
    subprocess.run([binary],check=True,timeout=10)
subprocess.run(['node','--check',str(ROOT/'assets/web/loxone.js')],check=True)

subprocess.run([os.sys.executable,str(ROOT/"tests/test_loxone_store.py")],check=True)
subprocess.run([os.sys.executable,str(ROOT/"tests/test_loxone_protocol.py")],check=True)
subprocess.run(['node',str(ROOT/'tests/test_loxone_web.js')],cwd=ROOT,check=True)

# A kézi automatikus OTA tesztgomb valódi HTTP-kezelője és JS-e.
subprocess.run([os.sys.executable, str(ROOT/"tests/test_ota_web.py")], check=True)
