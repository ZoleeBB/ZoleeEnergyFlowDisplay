#!/usr/bin/env python3
"""A közös SD-csomag a szerzői promptok és a nagy eredetik nélkül készül."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def home_resource_files():
    source = ROOT / 'assets/loxone-home'
    catalog = json.loads((source / 'catalog.json').read_text(encoding='utf-8'))
    icons = catalog['icons']
    if not 1 <= len(icons) <= 96 or len({icon['id'] for icon in icons}) != len(icons):
        raise ValueError('A home csomaghoz 1–96 egyedi ikon szükséges')
    document = {
        'schema': 1, 'id': 'loxone-home', 'kind': 'loxone-home-resources',
        'name': 'Loxone Otthon – közös erőforrások', 'version': 4,
        'consumer_backgrounds': {'light': 'consumers-light.png', 'dark': 'consumers-dark.png'},
        'icons': [{'id': icon['id'], 'file': 'icon-' + icon['id'] + '.png'} for icon in icons],
    }
    files = {f'background-{mode}.png': source / 'backgrounds' / f'{mode}.png' for mode in ('light', 'dark')}
    files.update({f'consumers-{mode}.png': source / 'backgrounds' / f'consumers-{mode}.png' for mode in ('light', 'dark')})
    files.update({f'preview-{mode}.png': source / 'previews' / f'cards-{mode}.png' for mode in ('light', 'dark')})
    files['loxone-logo.png'] = ROOT / 'assets/loxone-logo.png'
    document['audio_skins'] = {'light': 'audio-player-day.png', 'dark': 'audio-player-night.png'}
    skins = catalog.get('audio_skins', [])
    if len(skins) != 2 or {skin['mode'] for skin in skins} != {'day', 'night'}:
        raise ValueError('Mindkét Audio-lejátszóburkolat szükséges')
    for skin in skins:
        mode = skin['mode']
        if skin['file'] != f'skins/audio-player-{mode}.png':
            raise ValueError('Nem biztonságos Audio-burkolatútvonal')
        path = source / skin['file']
        data = path.read_bytes()
        if (hashlib.sha256(data).hexdigest() != skin['sha256'] or
                skin['width'] != 292 or skin['height'] != 200 or
                data[:16] != b'\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR' or
                data[16:24] != (292).to_bytes(4, 'big') + (200).to_bytes(4, 'big')):
            raise ValueError('Eltérő Audio-lejátszóburkolat: ' + mode)
        files[f'audio-player-{mode}.png'] = path
    for mode in ('light', 'dark'):
        document[mode] = {
            'background': '#0C1415' if mode == 'dark' else '#E7EDE5',
            'elements': [{'type': 'image', 'file': f'background-{mode}.png', 'x': 0, 'y': 0, 'w': 1280, 'h': 800}],
        }
    for icon in icons:
        path = source / icon['file']
        if hashlib.sha256(path.read_bytes()).hexdigest() != icon['sha256']:
            raise ValueError('Eltérő ikon: ' + icon['id'])
        files['icon-' + icon['id'] + '.png'] = path
    data = {'template.json': json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode()}
    data.update({name: path.read_bytes() for name, path in files.items()})
    return data


def package_home_resources(output):
    data = home_resource_files()
    output.mkdir(parents=True, exist_ok=True)
    archive_path = output / 'loxone-home.zip'
    # Rögzített dátum és rendezett nevek: azonos forrásból azonos csomag készül.
    with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(data):
            info = zipfile.ZipInfo(name, (2026, 10, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data[name])
    print('loxone-home:', len([name for name in data if name.startswith('icon-')]),
          'icons, 2 audio skins, 4 backgrounds, local logo,', archive_path.stat().st_size, 'bytes')
    return archive_path


if __name__ == '__main__':
    package_home_resources(ROOT / 'dist/templates')
