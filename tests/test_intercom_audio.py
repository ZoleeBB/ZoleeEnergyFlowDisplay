#!/usr/bin/env python3
"""Valódi kaputelefon-hangprotokoll ASan/UBSan alatt, hálózati hardver nélkül."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
subprocess.run([os.sys.executable, str(ROOT / 'tests/test_intercom_i2s.py')], check=True)
with tempfile.TemporaryDirectory(prefix='intercom-audio-') as directory:
    # A korábbi hamis HTTP-kliens engedélyezettnek tekintette a firmware-ből
    # ténylegesen kihagyott hitelesítést. A valódi buildvédelmet is fordítjuk.
    flags = ('CONFIG_ESP_HTTP_CLIENT_ENABLE_BASIC_AUTH', 'CONFIG_ESP_HTTP_CLIENT_ENABLE_DIGEST_AUTH',
             'CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM', 'CONFIG_SPIRAM_XIP_FROM_PSRAM')
    defaults = dict(line.split('=', 1) for line in (ROOT / 'sdkconfig.defaults').read_text().splitlines()
                    if line.startswith('CONFIG_') and '=' in line)
    probe = Path(directory) / 'probe.c'
    probe.write_text('#define ESP_PLATFORM 1\n#include "app_intercom_build_config.h"\n')
    for absent in (None, *flags):
        (Path(directory) / 'sdkconfig.h').write_text(''.join(
            f'#define {flag} 1\n' for flag in flags if defaults.get(flag) == 'y' and flag != absent))
        compiled = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Werror',
            '-I' + directory, '-Imain', '-c', str(probe), '-o', str(Path(directory) / 'probe.o')],
            cwd=ROOT, capture_output=True, text=True)
        if absent is None:
            assert compiled.returncode == 0, compiled.stderr
        else:
            assert compiled.returncode != 0 and 'Intercom requires' in compiled.stderr, absent
    print('PASS: firmware enables Basic/Digest and external stacks; actual compile guard rejects every missing capability')
    # A hálózati parser kis veremkerete nem függhet a 6 KiB-os SIP limitjétől.
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=1024', '-Imain', '-c', 'main/app_intercom_audio_protocol.c',
        '-o', str(Path(directory) / 'protocol-stack.o')], cwd=ROOT, check=True)
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

    video = str(Path(directory) / 'video')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Werror=frame-larger-than=8192', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-Itests/audio_stubs', '-Itests/stubs', '-Imain', 'tests/test_intercom_video_lifecycle.c',
        'main/app_intercom_stream.c', '-pthread', '-o', video], cwd=ROOT, check=True)
    subprocess.run([video], check=True, timeout=10, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
