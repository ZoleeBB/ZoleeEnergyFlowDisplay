#!/usr/bin/env python3
"""A GCC .su adataiból ellenőrzi a világítás, IRC és árnyékolás helyi veremkereteit.

Ez nem teljes taskverem-mérés: a libc/cJSON/LVGL és a megszakítások további
vermet használnak. A feladatméretek és a hardveres high-water próba megmarad.
"""
import argparse
from pathlib import Path
import re
import sys

FRAME_LIMIT = 1536
PREFIXES = ('lx_light_', 'light_', 'lighting_', 'app_loxone_light_', 'lx_home_values_',
            'lx_irc_', 'irc_', 'app_loxone_irc_', 'home_irc_',
            'lx_shading_', 'shading_', 'app_loxone_shading_', 'home_shading_')
GUARDED = {'app_loxone_home_visible', 'home_values_update_range',
           'home_command_build_locked', 'home_command_request', 'lx_home_shading_position_build'}
CALLERS = {'worker', 'runtime', 'reply', 'pump', 'wait_reply', 'update_values',
           'home_command_poll', 'home_render', 'home_build', 'home_prepare'}
REQUIRED = {'lx_light_catalog_attach', 'lx_light_block_event', 'lx_light_command_build',
            'lx_home_values_update_visible', 'lx_irc_catalog_attach', 'lx_irc_block_event',
            'lx_shading_catalog_attach', 'lx_shading_block_event'}
RECORD = re.compile(r'^(.*):(\d+):(\d+):([^\t]+)\t(\d+)\t(.+)$')


def parse_record(line):
    match = RECORD.fullmatch(line.strip())
    if not match:
        raise ValueError('Ismeretlen .su sor: ' + line[:160])
    source, _, _, name, size, qualifier = match.groups()
    return source, name, int(size), qualifier.strip()


def lighting_name(name):
    return name.startswith(PREFIXES) or name.split('.')[0] in GUARDED


def inspect(root, model_only=False):
    records = []
    for path in sorted(root.rglob('*.su')):
        # Csak a kiválasztott négy valódi fordítási egység oldalfájljai.
        if not any(unit in path.name for unit in
                   ('app_loxone_model', 'app_loxone_home_runtime', 'app_loxone.', 'app_ui.')):
            continue
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.strip():
                records.append(parse_record(line))
    failures = []
    names = {row[1].split('.')[0] for row in records}
    required = set(REQUIRED)
    if not model_only:
        required.update(('app_loxone_light_open', 'app_loxone_light_set', 'app_loxone_irc_summary',
                         'app_loxone_shading_summary', 'app_loxone_shading_command',
                         'app_loxone_shading_position'))
    for missing in sorted(required - names):
        failures.append('Hiányzó fordítói veremadat: ' + missing)
    if not model_only:
        for source, label in (('app_loxone_lighting_ui.inc', 'világítási'),
                              ('app_loxone_irc_ui.inc', 'IRC'),
                              ('app_loxone_shading_ui.inc', 'árnyékolási')):
            if not any(row[0].endswith(source) for row in records):
                failures.append(f'Hiányzó tényleges {label} UI-veremadat.')
    selected = [row for row in records if lighting_name(row[1])]
    for source, name, size, qualifier in selected:
        if size > FRAME_LIMIT:
            failures.append(f'{name}: {size} B > {FRAME_LIMIT} B')
        # A bounded érték a hívási argumentumok átmeneti veremhelyét is
        # tartalmazó felső korlát; a puszta dynamic nem bizonyított maximum.
        if qualifier not in ('static', 'dynamic,bounded'):
            failures.append(f'{name}: nem korlátos vagy ismeretlen veremkeret ({qualifier})')
    for _, name, size, qualifier in sorted(selected, key=lambda row: (-row[2], row[1])):
        print(f'LIGHTING STACK {name}: {size} B ({qualifier})')
    print('Hívókeretek; nem teljes hívásilánc-mélységek:')
    for _, name, size, qualifier in records:
        if name.split('.')[0] in CALLERS:
            print(f'CALLER STACK {name}: {size} B ({qualifier})')
    if failures:
        raise ValueError('\n'.join(failures))
    print(f'PASS: a világítás, IRC és árnyékolás {len(selected)} kerete <= {FRAME_LIMIT} B; '
          'a teljes taskverem külön ellenőrzendő.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--model-only', action='store_true')
    args = parser.parse_args()
    try:
        inspect(args.build_dir, args.model_only)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
