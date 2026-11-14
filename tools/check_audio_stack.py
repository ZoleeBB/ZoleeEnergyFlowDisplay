#!/usr/bin/env python3
"""Az Audio meglévő feladatvermekre költözésének valódi GCC-keretellenőrzése.

A helyi keret nem teljes hívási lánc: a libc, HTTP/TLS, cJSON és dekóderek
további vermet használnak. A main 10 KiB-os és az OTA 8 KiB-os mérete marad;
a panelen mért mélypont és DMA-tartalék ezek mellett is szükséges.
"""
import argparse
from pathlib import Path
import sys

from check_lighting_stack import parse_record


UNITS = {
    'app_main.c',
    'app_loxone_audio_runtime.c',
    'app_loxone_audio_auth.c',
    'app_loxone_audio_media.c',
    'app_loxone_audio_image.c',
    'app_ota_auto.c',
}
REQUIRED = {
    'app_main', 'app_boot', 'app_loxone_audio_run',
    'lx_audio_auth_create', 'app_loxone_audio_media_request', 'media_worker_step',
    'lx_audio_image_decode', 'ota_worker',
}
FRAME_LIMIT = 1536
WRAPPER_LIMITS = {'app_main': 256, 'ota_worker': 512}


def inspect(root, ota_only=False):
    units = {'app_ota_auto.c'} if ota_only else UNITS
    required = {'ota_worker'} if ota_only else REQUIRED
    records = []
    found_units = set()
    for path in sorted(root.rglob('*.su')):
        unit = next((name for name in units if path.name == name + '.su'), None)
        if unit is None:
            continue
        found_units.add(unit)
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.strip():
                records.append(parse_record(line))

    failures = ['Hiányzó fordítási egység veremadata: ' + unit
                for unit in sorted(units - found_units)]
    names = {name.split('.')[0] for _, name, _, _ in records}
    failures.extend('Hiányzó fordítói veremadat: ' + name
                    for name in sorted(required - names))
    for _, name, size, qualifier in records:
        limit = WRAPPER_LIMITS.get(name.split('.')[0], FRAME_LIMIT)
        if size > limit:
            failures.append(f'{name}: {size} B > {limit} B')
        if qualifier not in ('static', 'dynamic,bounded'):
            failures.append(f'{name}: nem korlátos veremkeret ({qualifier})')

    for source, name, size, qualifier in sorted(records, key=lambda row: (-row[2], row[1]))[:12]:
        print(f'AUDIO STACK {Path(source).name} {name}: {size} B ({qualifier})')
    for _, name, size, qualifier in records:
        if name.split('.')[0] in required:
            print(f'AUDIO ENTRY {name}: {size} B ({qualifier})')
    if failures:
        raise ValueError('\n'.join(failures))
    print(f'PASS: {len(records)} Audio/OTA keret <= {FRAME_LIMIT} B; '
          'a main/OTA belépési keret külön korlátozott. Nem teljes taskverem-mérés.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--ota-only', action='store_true')
    args = parser.parse_args()
    try:
        inspect(args.build_dir, args.ota_only)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
