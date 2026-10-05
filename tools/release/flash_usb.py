#!/usr/bin/env python3
"""Portable USB installer. All offsets and flash options come from the build."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys


def command(root, port, baud):
    manifest = json.loads((root / 'flasher_args.json').read_text(encoding='utf-8'))
    checksums = json.loads((root / 'flash-sha256.json').read_text(encoding='utf-8'))
    for name, expected in checksums.items():
        path = (root / name).resolve()
        if not path.is_relative_to(root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'Sérült vagy hiányzó telepítőfájl: {name}')
    extra = manifest['extra_esptool_args']
    result = [sys.executable, '-m', 'esptool', '--chip', extra['chip'], '--port', port,
              '--baud', str(baud), '--before', extra['before'].replace('_', '-'),
              '--after', extra['after'].replace('_', '-')]
    if not extra['stub']:
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
    parser.add_argument('--baud', type=int, default=460800)
    parser.add_argument('--dry-run', action='store_true', help='Ellenőrzés és parancskiírás, flash írás nélkül')
    args = parser.parse_args()
    cmd = command(Path(__file__).resolve().parent, args.port, args.baud)
    if args.dry_run:
        print(json.dumps(cmd, ensure_ascii=False))
        return
    if importlib.metadata.version('esptool') != '5.1.0':
        raise SystemExit('Telepítsd a rögzített verziót: python -m pip install esptool==5.1.0')
    subprocess.run(cmd, check=True)


if __name__ == '__main__':
    main()
