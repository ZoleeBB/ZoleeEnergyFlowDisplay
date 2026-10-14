#!/usr/bin/env python3
"""Run actual C modules with mocked time, RTOS locks and SD fault injection."""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
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
# Run the complete HTTP sample with stubbed Loxone I/O, including unchanged inputs.
subprocess.run([os.sys.executable, str(ROOT/"tests/test_http_picoc.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_release.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_auto_release.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_templates.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/'tests/test_web_preview.py')], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_memory.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_display_rotation.py")], check=True)
