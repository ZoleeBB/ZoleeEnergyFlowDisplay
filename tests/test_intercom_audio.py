#!/usr/bin/env python3
"""Valódi kaputelefon-hangprotokoll ASan/UBSan alatt, hálózati hardver nélkül."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-audio-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-Imain', 'tests/test_intercom_audio_protocol.c', 'main/app_intercom_audio_protocol.c',
        '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=20, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})

    session = str(Path(directory) / 'session')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-Itests/audio_stubs', '-Itests/stubs', '-Imain', 'tests/test_intercom_audio_session.c',
        'main/app_intercom_audio_protocol.c', '-pthread', '-o', session], cwd=ROOT, check=True)
    subprocess.run([session], check=True, timeout=25, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
