"""A tényleges OTA-végpont és a két nyelvű oldalon levő JavaScript ellenőrzése."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temporary:
    binary = str(Path(temporary) / 'test_ota_web')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-Itests/stubs', '-Imain', 'tests/test_ota_web.c', '-o', binary],
                   cwd=root, check=True)
    subprocess.run([binary], check=True)
    source = (root / 'main/app_web.c').read_text()
    for language in ('hu', 'en'):
        match = re.search(r'static const char page_' + language + r'\[\] =\s*(.*?);\n',
                          source, re.S)
        assert match
        page = ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))
        script = page.split('<script>', 1)[1].split('</script>', 1)[0]
        path = Path(temporary) / (language + '.js')
        path.write_text(script)
        subprocess.run(['node', '--check', str(path)], check=True)
        subprocess.run(['node', str(root / 'tests/test_ota_web.js'), str(path)], check=True)
