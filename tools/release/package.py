#!/usr/bin/env python3
"""Package the actual ESP-IDF flash manifest; never guess flash addresses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
from version import validate_tag

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + '\n').encode('utf-8')


def package(build, output, tag, commit):
    validate_tag(tag)
    if len(commit) != 40 or any(c not in '0123456789abcdef' for c in commit):
        raise ValueError('A full source commit SHA is required')
    description = json.loads((build / 'project_description.json').read_text())
    manifest = json.loads((build / 'flasher_args.json').read_text())
    if description['target'] != 'esp32p4' or manifest['extra_esptool_args']['chip'] != 'esp32p4':
        raise ValueError('Wrong build target')
    if description['project_version'] != tag:
        raise ValueError('Build version does not match the release tag')
    files = {}
    portable = {}
    ranges = []
    for address, name in manifest['flash_files'].items():
        source = (build / name).resolve()
        if not source.is_relative_to(build.resolve()) or not source.is_file():
            raise ValueError(f'Missing or external flash input: {name}')
        relative = source.relative_to(build.resolve()).as_posix()
        data = source.read_bytes()
        start = int(address, 0)
        if not data or start < 0 or any(start < end and start + len(data) > previous for previous, end in ranges):
            raise ValueError('Empty or overlapping flash input')
        ranges.append((start, start + len(data)))
        portable[hex(start)] = relative
        files[relative] = data
    app_name = Path(description['app_bin']).name
    if app_name not in files:
        raise ValueError('Application missing from flash manifest')
    if not any('bootloader' in name for name in files) or not any('partition' in name for name in files):
        raise ValueError('Incomplete USB manifest: bootloader or partition table missing')
    app = files[app_name]
    if len(app) < 288 or app[0] != 0xE9 or struct.unpack_from('<I', app, 32)[0] != 0xABCD5432:
        raise ValueError('Not a raw ESP application image')
    if app[48:80].split(b'\0', 1)[0].decode('ascii') != tag:
        raise ValueError('Embedded firmware version does not match the release tag')
    # Preserve all authoritative options, remove host build-directory paths.
    portable_manifest = {key: manifest[key] for key in ('write_flash_args', 'flash_settings', 'extra_esptool_args')}
    portable_manifest['flash_files'] = portable
    files['flasher_args.json'] = encoded(portable_manifest)
    files['flash-sha256.json'] = encoded({name: digest(data) for name, data in files.items()})
    files['flash.py'] = (ROOT / 'tools/release/flash_usb.py').read_bytes()
    files['TELEPITES.md'] = '# Telepítés\n\nLásd: [USB telepítés](docs/usb-install.md) és [SD-kártya](docs/sd-card.md).\n'.encode('utf-8')
    files['README.md'] = (ROOT / 'README.md').read_bytes()
    files['config.sample.json'] = (ROOT / 'config.sample.json').read_bytes()
    files['assets/template-manifest.json'] = (ROOT / 'assets/template-manifest.json').read_bytes()
    for folder in ('docs', 'examples'):
        for path in sorted((ROOT / folder).rglob('*')):
            if path.is_file():
                files[path.relative_to(ROOT).as_posix()] = path.read_bytes()
    for path in sorted((ROOT/'templates').glob('*/*')):
        if path.is_file() and path.suffix in ('.json','.png'):
            files['sd-card/templates/'+path.relative_to(ROOT/'templates').as_posix()] = path.read_bytes()
    info = {'tag': tag, 'commit': commit, 'target': 'esp32p4', 'board': 'Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1',
            'esp_idf': description['git_revision'], 'esptool': '5.1.0', 'application_sha256': digest(app),
            'flash_files': {address: {'file': name, 'sha256': digest(files[name])} for address, name in portable.items()}}
    files['build-info.json'] = encoded(info)
    output.mkdir(parents=True, exist_ok=True)
    prefix = f'ZoleeEnergyFlowDisplay-{tag}'
    ota = output / f'{prefix}-ota.bin'
    usb = output / f'{prefix}-usb.zip'
    ota.write_bytes(app)
    with zipfile.ZipFile(usb, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    (output / 'build-info.json').write_bytes(encoded(info))
    checks = [ota, usb, output / 'build-info.json']
    (output / 'SHA256SUMS.txt').write_text(''.join(f'{digest(path.read_bytes())}  {path.name}\n' for path in checks), encoding='ascii')
    print(f'Packaged {tag}: {len(portable)} flash images; OTA SHA256 {digest(app)}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--output', type=Path, default=Path('dist'))
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    package(args.build, args.output, args.tag, args.commit)
