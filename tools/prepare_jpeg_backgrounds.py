#!/usr/bin/env python3
"""Opcionális JPEG kísérők az átlátszatlan, teljes képernyős témahátterekhez (#17).

A sablon JSON-ja változatlanul a PNG-re hivatkozik; a firmware csak bekapcsolt
`display.jpeg_backgrounds` mellett keresi a PNG melletti azonos nevű `.jpg`-t,
és a P4 hardveres JPEG-motorjával bontja ki. Kikapcsolva (alapértelmezés) és
régebbi firmware-en minden a PNG-úton marad.

Csak olyan háttér kap JPEG kísérőt, amely teljes képernyős (1280x800) és
teljesen átlátszatlan: az alfa csatornás hátterek a jelenet színére olvadnak rá,
azt a JPEG nem tudja visszaadni. A minőséget úgy választjuk, hogy a csomag
kódolt mérete az APP_TEMPLATE_ENCODED_MAX alatt maradjon.

Használat: python3 tools/prepare_jpeg_backgrounds.py [--check]
"""
import io
import json
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / 'templates'
ENCODED_MAX = 5 * 1024 * 1024       # APP_TEMPLATE_ENCODED_MAX
BUDGET = ENCODED_MAX - 384 * 1024   # Tartalék a későbbi PNG-változásoknak.
QUALITIES = (92, 90, 88, 85)
CHECK = sys.argv[1:] == ['--check']
if sys.argv[1:] and not CHECK:
    sys.exit('Usage: python3 tools/prepare_jpeg_backgrounds.py [--check]')


def backgrounds(folder):
    """Teljes képernyős, átlátszatlan hátterek a sablon jeleneteiből."""
    document = json.loads((folder / 'template.json').read_text(encoding='utf-8'))
    names = []
    for variant in ('light', 'dark'):
        for element in document[variant]['elements']:
            if element.get('type') != 'image' or element.get('w') != 1280 or element.get('h') != 800:
                continue
            name = element.get('file', '')
            if name in names or not name.endswith('.png'):
                continue
            with Image.open(folder / name) as image:
                if image.size != (1280, 800):
                    continue
                if image.mode != 'RGB' and image.getchannel('A').getextrema() != (255, 255):
                    continue
            names.append(name)
    return names


def encode(folder, names):
    """A csomag méretkeretébe még beleférő legjobb minőség."""
    fixed = sum(file.stat().st_size for file in folder.iterdir()
                if file.is_file() and file.suffix != '.jpg')
    for quality in QUALITIES:
        encoded = {}
        for name in names:
            with Image.open(folder / name) as image:
                buffer = io.BytesIO()
                # 4:4:4: a finom UI-kontrasztok mellett nem mosódik a szín.
                image.convert('RGB').save(buffer, 'JPEG', quality=quality, optimize=True,
                                          subsampling=0)
                encoded[name] = buffer.getvalue()
        if fixed + sum(len(data) for data in encoded.values()) <= BUDGET:
            return quality, encoded
    return None, {}


def main():
    stale = []
    for folder in sorted(p for p in PACKAGES.iterdir() if (p / 'template.json').is_file()):
        names = backgrounds(folder)
        quality, encoded = encode(folder, names) if names else (None, {})
        wanted = {name[:-4] + '.jpg' for name in encoded}
        for existing in sorted(folder.glob('*.jpg')):
            if existing.name in wanted:
                continue
            stale.append(existing.relative_to(ROOT).as_posix())
            if not CHECK:
                existing.unlink()
        for name, data in encoded.items():
            target = folder / (name[:-4] + '.jpg')
            if target.is_file() and target.read_bytes() == data:
                continue
            stale.append(target.relative_to(ROOT).as_posix())
            if not CHECK:
                target.write_bytes(data)
        if encoded:
            print(f'{folder.name}: {len(encoded)} JPEG kísérő, minőség {quality}, '
                  f'{sum(len(d) for d in encoded.values()) // 1024} KiB')
    if CHECK and stale:
        sys.exit('Stale JPEG backgrounds:\n' + '\n'.join(stale) +
                 '\nRun: python3 tools/prepare_jpeg_backgrounds.py')


if __name__ == '__main__':
    main()
