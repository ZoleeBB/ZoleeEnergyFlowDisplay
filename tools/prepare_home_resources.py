#!/usr/bin/env python3
"""A közös SD-csomag a szerzői promptok és a nagy eredetik nélkül készül."""
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def home_resource_files():
    source = ROOT / 'assets/loxone-home'
    catalog = json.loads((source / 'catalog.json').read_text())
    icons = catalog['icons']
    if len(icons) != 64 or len({icon['id'] for icon in icons}) != 64:
        raise ValueError('A home csomaghoz pontosan 64 egyedi ikon szükséges')
    document = {
        'schema': 1, 'id': 'loxone-home', 'kind': 'loxone-home-resources',
        'name': 'Loxone Otthon – közös erőforrások', 'version': 1,
        'icons': [{'id': icon['id'], 'file': 'icon-' + icon['id'] + '.png'} for icon in icons],
    }
    files = {f'background-{mode}.png': source / 'backgrounds' / f'{mode}.png' for mode in ('light', 'dark')}
    files['loxone-logo.png'] = ROOT / 'assets/loxone-logo.png'
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
    print('loxone-home: 64 icons, 2 backgrounds, local logo,', archive_path.stat().st_size, 'bytes')
    return archive_path


if __name__ == '__main__':
    package_home_resources(ROOT / 'dist/templates')
