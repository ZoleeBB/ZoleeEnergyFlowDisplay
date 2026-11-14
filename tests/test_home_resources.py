#!/usr/bin/env python3
"""Valódi v4 csomag, mérethatárok és régi csomagok a C-telepítővel és a webes JS-sel."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_home_resources import package_home_resources

MIB = 1024 * 1024


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


def png(width, height):
    """Kicsi, valóban dekódolható RGBA kép kizárólag a mérethatár-fixture-ökhöz."""
    rows = (b'\0' + b'\x20\x40\x60\xff' * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def encode_document(files, document):
    files['template.json'] = json.dumps(document, ensure_ascii=False, separators=(',', ':')).encode()
    return files


def measure(files):
    encoded = sum(len(data) for name, data in files.items() if name != 'template.json')
    decoded = sum(struct.unpack('>I', data[16:20])[0] * struct.unpack('>I', data[20:24])[0] * 4
                  for name, data in files.items() if name != 'template.json' and not name.startswith('preview-'))
    return encoded, decoded


def pad_encoded(files, target):
    """Érvényes ancillary chunk növeli a PNG-bájtkeretet a dekódolt méret megváltoztatása nélkül."""
    files = files.copy()
    remaining = target - measure(files)[0]
    preferred = [name for name in files if name.startswith(('background-', 'consumers-'))]
    names = preferred or [name for name in files if name != 'template.json']
    for name in names:
        data = files[name]
        if not remaining:
            break
        if name == 'template.json':
            continue
        growth = min(remaining, 5 * MIB // 2 - len(data))
        if growth < 12:
            continue
        files[name] = data[:-12] + chunk(b'npAD', b'\0' * (growth - 12)) + data[-12:]
        remaining -= growth
    assert remaining == 0, remaining
    assert measure(files)[0] == target
    return files


with tempfile.TemporaryDirectory(prefix='home-resources-') as directory:
    folder = Path(directory)
    # A Windows-szimulátor CI-jében a magyar katalógus cp1252 olvasása hibát
    # adott. A tényleges generátort itt is nem UTF-8 alapértelmezéssel járatjuk.
    original_read_text = Path.read_text

    def windows_read_text(path, encoding=None, errors=None):
        return original_read_text(path, encoding=encoding or 'cp1252', errors=errors)

    with patch.object(Path, 'read_text', windows_read_text):
        archive = package_home_resources(folder)
    with zipfile.ZipFile(archive) as source:
        original = {name: source.read(name) for name in source.namelist()}
    original_doc = json.loads(original['template.json'])
    assert original_doc['schema'] == 1 and original_doc['version'] == 4
    assert len(original_doc['icons']) == 79
    assert original_doc['audio_skins'] == {'light': 'audio-player-day.png', 'dark': 'audio-player-night.png'}
    assert len(original) == 89
    assert all(len(data) <= 32768 for name, data in original.items() if name.startswith('icon-'))
    assert len(original['audio-player-day.png']) <= 262144 and len(original['audio-player-night.png']) <= 262144
    assert archive.stat().st_size <= 8 * MIB
    encoded, decoded = measure(original)
    assert encoded <= 6 * MIB and decoded <= 20 * MIB
    cases = []

    def fixture(name, files, *, valid=False, operation='probe', version=None):
        target = folder / (name + '.zip')
        # A szintetikus variánsokat nem tömörítjük újra; a valós csomag a generátor ZIP-je.
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_STORED) as output:
            for filename, body in files.items():
                output.writestr(filename, body)
        case = dict(name=name, path=str(target), operation=operation, valid=valid)
        if version is not None:
            case['version'] = version
        if valid and operation != 'unzip':
            case['encoded'], case['decoded'] = measure(files)
        cases.append(case)
        return target

    # A valós csomag hibáinál a korábbi telepítésnek érintetlenül kell maradnia.
    for name, mutation in (
        ('declared-icon-missing', lambda f, d: f.pop('icon-washing-machine.png')),
        ('extra-image', lambda f, d: f.update({'extra.png': f['icon-washing-machine.png']})),
        ('background-missing', lambda f, d: f.pop('consumers-dark.png')),
        ('wrong-background-name', lambda f, d: d['consumer_backgrounds'].update(dark='wrong.png')),
        ('skin-missing', lambda f, d: f.pop('audio-player-night.png')),
        ('skin-one-sided', lambda f, d: d['audio_skins'].pop('dark')),
        ('skin-wrong-name', lambda f, d: d['audio_skins'].update(light='renamed-player.png')),
        ('skin-undeclared', lambda f, d: d.pop('audio_skins')),
        ('skin-extra-mode', lambda f, d: d['audio_skins'].update(other='audio-player-day.png')),
        ('skin-wrong-width', lambda f, d: f.update({'audio-player-day.png': png(291, 200)})),
        ('skin-wrong-height', lambda f, d: f.update({'audio-player-night.png': png(292, 201)})),
        ('icon-wrong-size', lambda f, d: f.update({'icon-washing-machine.png': png(95, 96)})),
        ('icon-duplicate', lambda f, d: d['icons'].append(d['icons'][0].copy())),
        ('preview-single', lambda f, d: f.pop('preview-dark.png')),
    ):
        files = original.copy()
        document = copy.deepcopy(original_doc)
        mutation(files, document)
        fixture(name, encode_document(files, document), operation='install')
    corrupt = original.copy()
    damaged = bytearray(corrupt['icon-washing-machine.png'])
    damaged[-1] ^= 1
    corrupt['icon-washing-machine.png'] = damaged
    fixture('icon-png-crc', corrupt, operation='install')

    # Valódi méretű, kisméretű fájlok: a határtesztet nem a képi tömöríthetőség dönti el.
    compact = {}
    for name, data in original.items():
        if name != 'template.json':
            width, height = struct.unpack('>II', data[16:24])
            compact[name] = png(width, height)
    encode_document(compact, copy.deepcopy(original_doc))

    def icon_count(count, version=4, skins=True):
        files = {name: data for name, data in compact.items() if not name.startswith('icon-')}
        document = copy.deepcopy(original_doc)
        document['version'] = version
        icons = copy.deepcopy(original_doc['icons'][:min(count, 64)])
        while len(icons) < count:
            identity = 'boundary-' + str(len(icons))
            icons.append({'id': identity, 'file': 'icon-' + identity + '.png'})
        document['icons'] = icons
        for icon in icons:
            files[icon['file']] = png(96, 96)
        if not skins:
            document.pop('audio_skins')
            files.pop('audio-player-day.png')
            files.pop('audio-player-night.png')
        if version == 1:
            document.pop('consumer_backgrounds')
            files.pop('consumers-light.png')
            files.pop('consumers-dark.png')
        return encode_document(files, document)

    fixture('v4-one-icon', icon_count(1), valid=True, version=4)
    fixture('v4-max96', icon_count(96), valid=True, version=4)
    fixture('v4-empty', icon_count(0))
    fixture('v4-over97', icon_count(97))
    fixture('v4-no-skins', icon_count(64, skins=False), valid=True, version=4)
    fixture('v3-legacy', icon_count(64, version=3, skins=False), valid=True, operation='install', version=3)
    fixture('v1-legacy', icon_count(64, version=1, skins=False), valid=True, operation='install', version=1)
    fixture('v3-under63', icon_count(63, version=3, skins=False))
    fixture('v3-over65', icon_count(65, version=3, skins=False))
    fixture('v3-new-skins', icon_count(64, version=3, skins=True))
    maximum_decoded = measure(icon_count(96))[1]
    assert 19 * MIB < maximum_decoded <= 20 * MIB
    fixture('home-above-old5mib', pad_encoded(compact, 5 * MIB + 1), valid=True, version=4)
    fixture('home-at6mib', pad_encoded(compact, 6 * MIB), valid=True, version=4)
    fixture('home-over6mib', pad_encoded(compact, 6 * MIB + 1))

    normal_doc = {'schema': 1, 'id': 'normal-boundary', 'name': 'Normál mérethatár', 'version': 1,
                  'light': {'background': '#000000', 'elements': []},
                  'dark': {'background': '#000000', 'elements': []}}
    normal = encode_document({f'normal-{i}.png': png(1, 1) for i in range(3)}, normal_doc)
    fixture('normal-at5mib', pad_encoded(normal, 5 * MIB), valid=True)
    fixture('normal-over5mib', pad_encoded(normal, 5 * MIB + 1))
    normal_decoded = encode_document({f'normal-{i}.png': png(1280, 800) for i in range(4)}, normal_doc)
    fixture('normal-over12mib-decoded', normal_decoded)

    # A ZIP-bejegyzéskeretet a sémától külön vizsgáljuk: 128-ból nem lesz 128 megengedett kép.
    for count in (128, 129):
        files = {'template.json': compact['template.json']}
        files.update({f'zip-{i}.png': b'png-placeholder' for i in range(count - 1)})
        fixture('home-zip-' + str(count), files, valid=count == 128, operation='unzip')
    for count in (34, 35):
        files = {'template.json': normal['template.json']}
        files.update({f'zip-{i}.png': b'png-placeholder' for i in range(count - 1)})
        fixture('normal-zip-' + str(count), files, valid=count == 34, operation='unzip')

    # A 8 MiB-os ZIP felső határát új, érvényes központi könyvtárú archívumokkal mérjük.
    archive_files = {'template.json': compact['template.json'], **{f'large-{i}.png': b'x' for i in range(4)}}
    probe_path = folder / 'zip-size-probe.zip'
    with zipfile.ZipFile(probe_path, 'w', zipfile.ZIP_STORED) as output:
        for name, body in archive_files.items():
            output.writestr(name, body)
    overhead = probe_path.stat().st_size - sum(map(len, archive_files.values()))
    available = 8 * MIB - overhead - len(archive_files['template.json'])
    for i in range(4):
        size = available // (4 - i)
        archive_files[f'large-{i}.png'] = b'x' * size
        available -= size
    exact = fixture('zip-at8mib', archive_files, valid=True, operation='unzip')
    assert exact.stat().st_size == 8 * MIB
    archive_files['large-0.png'] += b'x'
    over = fixture('zip-over8mib', archive_files, operation='unzip')
    assert over.stat().st_size == 8 * MIB + 1

    case_path = folder / 'cases.json'
    case_path.write_text(json.dumps(cases), encoding='utf-8')
    binary = str(folder / 'test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-Werror=frame-larger-than=8192', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', '-DAPP_TEMPLATE_DESKTOP', '-Isimulator/compat', '-Itests/stubs', '-Imain',
                    'tests/test_home_resources.c', 'main/app_templates.c', 'main/app_template_raw.c',
                    'main/app_template_zip.c', 'main/app_ringtone_wav.c', '-lz', '-lm', '-l:libcjson.so.1',
                    '-o', binary], cwd=ROOT, check=True)
    sd = folder / 'sd'
    sd.mkdir()
    subprocess.run([binary, str(archive), str(sd), str(case_path)], check=True, timeout=90)
    subprocess.run(['node', 'tests/test_home_resources_web.js', str(archive), str(case_path)],
                   cwd=ROOT, check=True, timeout=90)
