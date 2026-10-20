#!/usr/bin/env python3
"""Run actual C modules with mocked time, RTOS locks and SD fault injection."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_slave_display.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_loxone_home.py')], check=True)
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
    binary = str(Path(directory) / 'test_ble_slave')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ble_slave.c',
                    'main/app_ble_protocol.c', 'main/app_ble_stats_protocol.c', '-lm', '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
with tempfile.TemporaryDirectory(prefix='zolee-tests-') as directory:
    temp = Path(directory)
    sources = ['tests/test_core.c', 'main/app_payload.c', 'main/app_config.c',
               'main/app_http.c', 'main/app_ingest.c', 'main/app_stats.c', 'main/app_events.c', 'main/app_state.c']
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
assert 'app_display_boot_sync(' in main_source and 'orientation_verified_task' in main_source
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

subprocess.run([os.sys.executable, str(ROOT/'tests/test_web_preview.py')], check=True)
subprocess.run([os.sys.executable, str(ROOT/'tests/test_settings.py')], check=True)
subprocess.run(['node', str(ROOT/'tests/test_settings_web.js')], cwd=ROOT, check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_memory.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_rotation.py")], check=True)
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
