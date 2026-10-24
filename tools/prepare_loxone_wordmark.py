#!/usr/bin/env python3
"""A felhasználó SVG-logójának egyszeri, hordozható LVGL-változata.

A kész fejléc verziózott; firmware-fordításhoz nem kell SVG-könyvtár.
Újragenerálás: pip install cairosvg; python3 tools/prepare_loxone_wordmark.py.
"""
from pathlib import Path
import tempfile
import struct

root = Path(__file__).resolve().parents[1]
import cairosvg
with tempfile.TemporaryDirectory() as directory:
    png = Path(directory) / 'logo.png'
    cairosvg.svg2png(url=str(root / 'assets/loxone-wordmark.svg'), write_to=str(png),
                    output_width=400, output_height=88)
    data = png.read_bytes()
width, height = struct.unpack('>II', data[16:24])
lines = ['/* Generált fájl: tools/prepare_loxone_wordmark.py */', '#pragma once',
         '#include "lvgl.h"', 'static const uint8_t loxone_wordmark_png[] = {']
for start in range(0, len(data), 24):
    lines.append(','.join(f'0x{byte:02x}' for byte in data[start:start+24]) + ',')
lines += ['};', 'static const lv_image_dsc_t loxone_wordmark = {',
          '    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW_ALPHA,',
          f'               .w = {width}, .h = {height}}},',
          '    .data_size = sizeof(loxone_wordmark_png), .data = loxone_wordmark_png', '};']
(root / 'main/app_loxone_wordmark.h').write_text('\n'.join(lines) + '\n')
print('Loxone SVG:', width, 'x', height, ',', len(data), 'PNG bájt')
