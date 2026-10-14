#!/usr/bin/env python3
"""Portable USB installer. All offsets and flash options come from the build."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys


ESPTOOL_VERSION = "5.5.0"
DEFAULT_BAUD = 115200


def command(root, port, baud, no_stub=False, manual_boot=False):
    manifest = json.loads((root / 'flasher_args.json').read_text(encoding='utf-8'))
    checksums = json.loads((root / 'flash-sha256.json').read_text(encoding='utf-8'))
    for name, expected in checksums.items():
        path = (root / name).resolve()
        if not path.is_relative_to(root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Sérült vagy hiányzó telepítőfájl: {name}')
    extra = manifest['extra_esptool_args']
    use_stub = extra['stub'] and not no_stub
    if not use_stub:
        for offset, name in manifest['flash_files'].items():
            if int(offset, 0) + (root / name).stat().st_size > 16 * 1024 * 1024:
                raise ValueError('ROM feltöltésnél a 16 MiB feletti flash-terület nem támogatott; használd a stubot.')
    result = [sys.executable, '-m', 'esptool', '--chip', extra['chip'], '--port', port,
              '--baud', str(baud), '--before', 'no-reset' if manual_boot else extra['before'].replace('_', '-'),
              '--after', 'no-reset' if manual_boot else extra['after'].replace('_', '-')]
    if not use_stub:
        result.append('--no-stub')
    result.append('write-flash')
    result.extend(x.replace('_', '-') if x.startswith('--') else x for x in manifest['write_flash_args'])
    for offset, name in sorted(manifest['flash_files'].items(), key=lambda item: int(item[0], 0)):
        if name not in checksums:
            raise ValueError(f'Hiányzó ellenőrzőösszeg: {name}')
        result.extend([offset, str(root / name)])
    return result


def main():
    parser = argparse.ArgumentParser(description='Zolee Energy Flow Display USB első telepítés')
    parser.add_argument('--port', required=True, help='Például COM5 vagy /dev/ttyACM0')
    parser.add_argument('--baud', type=int, default=DEFAULT_BAUD)
    parser.add_argument('--no-stub', action='store_true', help='ROM feltöltés stub nélkül, legfeljebb 16 MiB címtartományban')
    parser.add_argument('--manual-boot', action='store_true', help='Kézi BOOT/RESET mód; nincs automatikus reset, feltöltés után RESET szükséges')
    parser.add_argument('--dry-run', action='store_true', help='Ellenőrzés és parancskiírás, flash írás nélkül')
    args = parser.parse_args()
    cmd = command(Path(__file__).resolve().parent, args.port, args.baud, args.no_stub, args.manual_boot)
    if args.dry_run:
        print(json.dumps(cmd, ensure_ascii=False))
        return
    try:
        installed = importlib.metadata.version('esptool')
    except importlib.metadata.PackageNotFoundError:
        installed = None
    shell_python = ('& ' if sys.platform == 'win32' else '') + f'"{sys.executable}"'
    if installed != ESPTOOL_VERSION:
        raise SystemExit(f'Telepítsd ugyanebbe a Python környezetbe: {shell_python} -m pip install esptool=={ESPTOOL_VERSION}')
    result = subprocess.run(cmd, check=False)
    if result.returncode:
        print('A feltöltés nem fejeződött be. USB/stub hibánál lépj kézi BOOT módba, majd próbáld: '
              f'{shell_python} "{Path(__file__).resolve()}" --port {args.port} --no-stub --manual-boot', file=sys.stderr)
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
