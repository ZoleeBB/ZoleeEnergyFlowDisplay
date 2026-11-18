#!/usr/bin/env python3
"""Az eredeti projektlogót a 10,1 hüvelykes indítólista méretére alakítja."""
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]


def main():
    with Image.open(ROOT / 'assets/branding/logo.png') as source:
        image = source.convert('RGBA')
    # A forrás üres szélén is akadnak szinte láthatatlan képpontok.
    alpha = image.getchannel('A').point(lambda value: value if value >= 4 else 0)
    image.putalpha(alpha)
    image = image.crop(alpha.getbbox())
    image.thumbnail((176, 107), Image.Resampling.LANCZOS)
    transparent = image.getchannel('A').point(lambda value: 255 if value < 3 else 0)
    image.paste((0, 0, 0, 0), mask=transparent)
    indexed = image.quantize(96, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    indexed.save(ROOT / 'main/assets/png/boot_logo.png', optimize=True)


if __name__ == '__main__':
    main()
