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
    # The repository's exact Pico C sprintf line must form a valid JSON payload.
    source = (ROOT/'examples/loxone-energy-payload.picoc').read_text()
    line = next(line for line in source.splitlines() if 'PowerGrid' in line and '%f' in line)
    c = '#include <stdio.h>\nint main(void){char json[512];sprintf(json,\n'+line+'\n'+','.join(['1.0']*12)+');puts(json);}\n'
    (temp/'picoc.c').write_text(c)
    subprocess.run(['cc', '-Wall', '-Werror', str(temp/'picoc.c'), '-o', str(temp/'picoc')], check=True)
    import json
    payload = json.loads(subprocess.check_output([str(temp/'picoc')], text=True))
    assert len(payload) == 12 and payload['BatteryTemperature'] == 1
    print('PASS: Pico C format produces a complete 12-field JSON payload')

# Run the complete HTTP sample with stubbed Loxone I/O, including unchanged inputs.
subprocess.run([os.sys.executable, str(ROOT/"tests/test_http_picoc.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_release.py")], check=True)

subprocess.run([os.sys.executable, str(ROOT/"tests/test_auto_release.py")], check=True)
subprocess.run([os.sys.executable, str(ROOT/"tests/test_templates.py")], check=True)
