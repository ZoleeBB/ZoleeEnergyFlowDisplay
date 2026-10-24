#!/usr/bin/env python3
"""Package the checked-in CC0 doorbells; no network is needed for a release."""
import json
from pathlib import Path
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def ringtone_files():
    folder = ROOT / 'assets/ringtones'
    manifest = json.loads((folder / 'ringtone.json').read_text())
    files = {'ringtone.json': (folder / 'ringtone.json').read_bytes()}
    for sound in manifest['sounds']:
        path = folder / sound['file']
        with wave.open(str(path)) as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (1, 2, 16000, 'NONE') or not 0 < audio.getnframes() <= 128000:
                raise ValueError('Unsupported ringtone: ' + path.name)
        if sound['license'] != 'CC0-1.0':
            raise ValueError('Starter pack must be redistributable CC0')
        files[path.name] = path.read_bytes()
    return files


def package_ringtones(output):
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(ringtone_files().items()):
            entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)


if __name__ == '__main__':
    output = ROOT / 'dist/ringtones.zip'
    output.parent.mkdir(parents=True, exist_ok=True)
    package_ringtones(output)
    print(output)
