#!/usr/bin/env python3
"""Natív 1280×800-as háttérképek a futásidejű skálázás helyett.

A 02/03/07 gyári témák 1024×640-es hátterét a firmware minden témaváltáskor
LVGL-lel 1280×800-ra skálázta (lv_snapshot), ami a betöltést lassította. Ez az
eszköz a ui_preview által ugyanazzal az LVGL-raszterizálóval előállított képpontokat
(ZOLEE_EXPORT_SCALED_BACKGROUNDS) menti el PNG-ként, így a kijelző képe
képpontra azonos marad, csak a skálázás marad el.

Használat:
  ZOLEE_EXPORT_SCALED_BACKGROUNDS=<dir> ZOLEE_TEMPLATE_ROOT=<sd> ui_preview
  python3 tools/prepare_native_backgrounds.py <dir>
"""
import hashlib
import json
import sys
from io import BytesIO
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LIMIT = 5 * 1024 * 1024 // 2  # APP_TEMPLATE_PNG_MAX
MANIFEST = ROOT / 'assets/template-manifest.json'
SCALED = 'LVGL rasterizer rescale to native 1280x800'


def manifest_update(target, encoded):
    """A generált képek nyilvántartása a tényleges tartalmat kövesse."""
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    asset = target.relative_to(ROOT).as_posix()
    for item in manifest:
        if item['asset'] != asset:
            continue
        item['sha256'] = hashlib.sha256(encoded).hexdigest()
        if SCALED not in item['generator']:
            item['generator'] = f"{item['generator']}; {SCALED}"
        MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n',
                            encoding='utf-8')
        return
    raise SystemExit(f'Missing manifest entry: {asset}')


def convert(raw_path):
    template, name = raw_path.name[:-len('.argb')].split('__', 1)
    target = ROOT / 'templates' / template / name
    if not target.is_file():
        raise SystemExit(f'Unknown target: {target}')
    data = raw_path.read_bytes()
    if len(data) != 1280 * 800 * 4:
        raise SystemExit(f'Unexpected raster size: {raw_path}')
    # LVGL ARGB8888 little-endian: B, G, R, A.
    image = Image.frombuffer('RGBA', (1280, 800), data, 'raw', 'BGRA', 0, 1)
    if image.getchannel('A').getextrema() == (255, 255):
        image = image.convert('RGB')
    output = BytesIO()
    image.save(output, format='PNG', optimize=True)
    encoded = output.getvalue()
    if len(encoded) > LIMIT:
        raise SystemExit(f'{target}: {len(encoded)} bytes exceeds {LIMIT}')
    target.write_bytes(encoded)
    manifest_update(target, encoded)
    print(f'{target.relative_to(ROOT)}: 1280x800 {image.mode}, {len(encoded)} bytes')


def main(directory):
    files = sorted(Path(directory).glob('*.argb'))
    if not files:
        raise SystemExit('No exported rasters found')
    for raw in files:
        convert(raw)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.preview-backgrounds')
