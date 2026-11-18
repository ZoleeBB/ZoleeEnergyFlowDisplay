"""A helyi reset végpont gazdagéptesztje, a tényleges hitelesítéssel és OTA-zárral."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
web = (root / 'main/app_web.c').read_text()
ota = (root / 'main/app_ota_auto.c').read_text()


def implementation(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


with tempfile.TemporaryDirectory(prefix='factory-reset-http-') as directory:
    work = Path(directory)
    # Ugyanazok a függvények fussanak, amelyeket az ESP HTTP-szerver használ.
    production = [
        implementation(web, 'static bool auth_enabled('),
        implementation(web, 'static bool authorize('),
        implementation(ota, 'bool app_ota_try_begin_operation('),
        implementation(ota, 'void app_ota_end_operation('),
        implementation(web, 'static esp_err_t exclusive_request('),
        implementation(web, 'static void reboot_task('),
        '#include "app_factory_reset_web.inc"',
        implementation(web, 'static esp_err_t factory_reset_post('),
    ]
    (work / 'reset_routes.inc').write_text('\n\n'.join(production))
    assert re.search(r'\.uri\s*=\s*"/api/factory-reset",\s*\.method\s*=\s*HTTP_POST,\s*'
                     r'\.handler\s*=\s*factory_reset_post', web)
    for name in ('settings', 'reboot', 'ota'):
        source = (root / 'main/app_settings_web.inc').read_text() if name == 'settings' else web
        assert f'exclusive_request(req, {name}_post_locked)' in implementation(source, f'static esp_err_t {name}_post(')
    binary = work / 'reset-web'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', '-I' + str(work), 'tests/test_factory_reset_web.c',
                    'main/app_http.c', '-o', str(binary)], cwd=root, check=True)
    subprocess.run([binary], check=True, timeout=10)
