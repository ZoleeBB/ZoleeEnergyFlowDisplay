#!/usr/bin/env python3
"""A méretezett, átlátszó termosztátgrafika natív 96×96-os SD-változata.

Csak arányos kicsinyítés és középre helyezés történik. A kijelző üres marad:
a célhőmérsékletet és az üzemmódjelzést az LVGL élő adatból rajzolja rá.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def prepare():
    source = ROOT / 'assets/source/irc-thermostat.png'
    target = ROOT / 'assets/loxone-home/icons/thermostat-irc.png'
    with Image.open(source) as original:
        image = original.convert('RGBA')
        image.thumbnail((96, 96), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (96, 96), (0, 0, 0, 0))
        canvas.alpha_composite(image, ((96 - image.width) // 2, (96 - image.height) // 2))
        canvas.save(target, optimize=True)
    print(f'{target.relative_to(ROOT)}: {target.stat().st_size} bytes, 96×96 RGBA')


if __name__ == '__main__':
    prepare()
