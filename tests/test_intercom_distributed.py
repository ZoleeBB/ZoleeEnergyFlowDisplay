#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-distributed-') as directory:
    for slave in (False, True):
        binary = str(Path(directory) / ('slave' if slave else 'master'))
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-Werror=frame-larger-than=8192', '-fsanitize=undefined', '-fno-sanitize-recover=all',
            '-ffunction-sections', '-Wl,--gc-sections', '-Itests/audio_stubs', '-Itests/stubs', '-Imain',
            *(['-DAPP_INTERCOM_SLAVE'] if slave else []), 'tests/test_intercom_distributed.c',
            'main/app_intercom_arbiter.c', 'main/app_ringtone_wav.c', '-pthread', '-o', binary], cwd=ROOT, check=True)
        subprocess.run([binary], check=True, timeout=15)
