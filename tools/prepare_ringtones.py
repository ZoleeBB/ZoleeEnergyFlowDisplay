#!/usr/bin/env python3
"""A manifestben felsorolt csengőhangok csomagolása, hálózati letöltés nélkül."""
import json
from pathlib import Path
import re
import wave
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAX_PACKAGE = 8 * 1024 * 1024


def ringtone_files(folder=None):
    folder = Path(folder) if folder is not None else ROOT / 'assets/ringtones'
    manifest_bytes = (folder / 'ringtone.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    sounds = manifest.get('sounds')
    if not isinstance(sounds, list) or not 1 <= len(sounds) <= 32:
        raise ValueError('Ringtone manifest must list 1–32 sounds')
    files = {'ringtone.json': manifest_bytes}
    for sound in sounds:
        name = sound.get('file') if isinstance(sound, dict) else None
        if not isinstance(name, str) or len(name) > 64 or not re.fullmatch(r'[a-z0-9_-]+\.wav', name):
            raise ValueError('Unsafe ringtone filename: ' + str(name))
        if name in files:
            raise ValueError('Duplicate ringtone: ' + name)
        # A forrás licence megmarad; a csomag nem minősít minden hangot CC0-nak.
        if not isinstance(sound.get('license'), str) or not sound['license'].strip():
            raise ValueError('Missing source license metadata: ' + name)
        path = folder / name
        if path.is_symlink():
            raise ValueError('Ringtone symlinks are not supported: ' + name)
        with wave.open(str(path)) as audio:
            frames = audio.getnframes()
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (1, 2, 16000, 'NONE') or not 0 < frames <= 960000:
                raise ValueError('Unsupported ringtone: ' + name)
            if len(audio.readframes(frames)) != frames * 2:
                raise ValueError('Truncated ringtone: ' + name)
        files[name] = path.read_bytes()
    if sum(map(len, files.values())) > MAX_PACKAGE:
        raise ValueError('Uncompressed ringtone package exceeds 8 MiB')
    return files


def package_ringtones(output, folder=None):
    files = ringtone_files(folder)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    if Path(output).stat().st_size > MAX_PACKAGE:
        Path(output).unlink()
        raise ValueError('Compressed ringtone package exceeds 8 MiB')


if __name__ == '__main__':
    output = ROOT / 'dist/ringtones.zip'
    output.parent.mkdir(parents=True, exist_ok=True)
    package_ringtones(output)
    print(output)
