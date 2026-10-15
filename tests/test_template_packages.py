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
    if not expected or expected.keys() != actual.keys():
        raise AssertionError(
            f'Template archives mismatch: missing={sorted(expected.keys()-actual.keys())}, '
            f'extra={sorted(actual.keys()-expected.keys())}'
        )
    for name, folder in expected.items():
        with zipfile.ZipFile(actual[name]) as archive:
            contents = {info.filename for info in archive.infolist()}
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
