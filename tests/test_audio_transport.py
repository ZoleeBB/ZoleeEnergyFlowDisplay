#!/usr/bin/env python3
"""A rögzített Espressif-küldő tényleges frame-határai, vendor-másolat nélkül.

A megadott forrásból változatlan függvénytörzsek csak az ideiglenes fordításba
kerülnek. Nincs hálózati letöltés vagy kézzel újraírt fragmentáló algoritmus.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def source_function(source, name):
    pattern = re.compile(r'(?m)^(?:static\s+)?int\s+' + re.escape(name) +
                         r'\s*\([^;{}]*\)\s*\{')
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f'Nem egyértelmű upstream függvény: {name}')
    match = matches[0]
    position = match.end()
    depth, state = 1, 'code'
    while position < len(source):
        char = source[position]
        pair = source[position:position + 2]
        if state == 'line':
            if char == '\n':
                state = 'code'
        elif state == 'block':
            if pair == '*/':
                state = 'code'
                position += 1
        elif state in ('"', "'"):
            if char == '\\':
                position += 1
            elif char == state:
                state = 'code'
        elif pair in ('//', '/*'):
            state = 'line' if pair == '//' else 'block'
            position += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if not depth:
                return source[match.start():position + 1]
        position += 1
    raise ValueError(f'Lezáratlan upstream függvény: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--websocket', type=Path,
                        default=os.environ.get('ESP_WEBSOCKET_CLIENT_SOURCE'),
                        help='A rögzített komponens esp_websocket_client.c fájlja')
    parser.add_argument('--sdkconfig', type=Path,
                        help='A valódi firmware-build sdkconfig fájlja')
    args = parser.parse_args()
    if args.sdkconfig:
        if not args.sdkconfig.is_file():
            parser.error('A megadott sdkconfig nem található: ' + str(args.sdkconfig))
        config = dict(re.findall(r'^(CONFIG_[A-Z0-9_]+)=(.*)$',
                                args.sdkconfig.read_text(encoding='utf-8'), re.MULTILINE))
        if config.get('CONFIG_SPIRAM_USE_MALLOC') != 'y' or \
                config.get('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL') != '0':
            parser.error('A build malloc beállítása nem igazolja a PSRAM-preferenciát')
        if not args.websocket:
            parser.error('--sdkconfig mellett --websocket vagy explicit forráskörnyezeti változó kell')
        print('PASS: actual sdkconfig enables PSRAM-preferred malloc with internal threshold 0')
    if not args.websocket:
        print('SKIP: Audio TX requires --websocket or ESP_WEBSOCKET_CLIENT_SOURCE; no upstream source fetched')
        return 0
    if not args.websocket.is_file():
        parser.error('A megadott WebSocket-forrás nem található: ' + str(args.websocket))
    raw = args.websocket.read_bytes()
    source = raw.decode('utf-8')
    names = ('esp_websocket_client_send_with_exact_opcode',
             'esp_websocket_client_send_with_opcode', 'esp_websocket_client_send_text')
    try:
        functions = [source_function(source, name) for name in names]
    except ValueError as error:
        parser.error(str(error))
    with tempfile.TemporaryDirectory(prefix='audio-transport-') as directory:
        temp = Path(directory)
        (temp / 'audio_upstream_send.inc').write_text('\n\n'.join(functions) + '\n', encoding='utf-8')
        binary = temp / 'transport'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Werror=frame-larger-than=1536',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-Imain', '-I' + str(temp), 'tests/test_audio_transport.c', '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=20)
    print('PASS: upstream send functions extracted unchanged; source SHA-256=' + hashlib.sha256(raw).hexdigest())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
