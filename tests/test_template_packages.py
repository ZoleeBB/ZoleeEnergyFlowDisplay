#!/usr/bin/env python3
"""Check that the template-packages artifact includes every source theme."""
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TEMPLATES = ROOT / 'templates'
DIST = ROOT / 'dist/templates'


def main():
    expected = {
        path.name: path
        for path in TEMPLATES.iterdir()
        if path.is_dir() and (path / 'template.json').is_file()
    }
    actual = {path.stem: path for path in DIST.glob('*.zip')}
    resources = actual.pop('loxone-home', None)
    assert resources, 'A közös Loxone-erőforráscsomag hiányzik'
    catalog = json.loads((ROOT / 'assets/loxone-home/catalog.json').read_text())
    with zipfile.ZipFile(resources) as archive:
        doc = json.loads(archive.read('template.json'))
        assert doc['kind'] == 'loxone-home-resources' and doc['version'] == 4
        assert [icon['id'] for icon in doc['icons']] == [icon['id'] for icon in catalog['icons']]
        assert len(archive.namelist()) == len(catalog['icons']) + 10
        assert doc['audio_skins'] == {'light':'audio-player-day.png','dark':'audio-player-night.png'}
        for skin in catalog['audio_skins']:
            name = 'audio-player-' + skin['mode'] + '.png'
            assert archive.read(name) == (ROOT / 'assets/loxone-home' / skin['file']).read_bytes()
        assert {'preview-light.png','preview-dark.png'} <= set(archive.namelist())
        assert doc['consumer_backgrounds'] == {'light':'consumers-light.png','dark':'consumers-dark.png'}
        assert b'prompt' not in archive.read('template.json')
        assert b'original_sha256' not in archive.read('template.json')
    if not expected or expected.keys() != actual.keys():
        raise AssertionError(
            f'Template archives mismatch: missing={sorted(expected.keys()-actual.keys())}, '
            f'extra={sorted(actual.keys()-expected.keys())}'
        )
    for name, folder in expected.items():
        with zipfile.ZipFile(actual[name]) as archive:
            contents = {info.filename for info in archive.infolist()}
            assert {'preview-light.png','preview-dark.png'} <= contents, name
            files = {path.name for path in folder.iterdir() if path.is_file()}
            assert contents == files, (name, sorted(files-contents), sorted(contents-files))
            for filename in files:
                assert archive.read(filename) == (folder/filename).read_bytes(), (name, filename)
            doc = json.loads(archive.read('template.json'))
            assert doc.get('id') == name, name
            assert 'light' in doc and 'dark' in doc, name
    print(f'PASS: all {len(expected)} template ZIPs match their source folders: '
          + ', '.join(sorted(actual)))


if __name__ == '__main__':
    main()
