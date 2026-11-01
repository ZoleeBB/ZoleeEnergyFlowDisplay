#!/usr/bin/env python3
"""A generált, átlátszó eredetiből natív méretű időjárásikon készítése.

Az alfa és az ikon rajzolata megmarad; csak a felbontás változik.
A 28×28/30×30-as képek ARGB8888-ként 3136/3600 bájtosak.
"""
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/source/weather_humidity.png'
PROMPT = 'Use case: stylized-concept. Asset type: a single tiny weather UI icon for relative humidity for an embedded energy display. Primary request: one beautiful luminous sapphire blue water droplet, rounded lower body, pointed curved tip, white pearlescent highlights, subtle cyan rim glow. Match the refined glossy dimensional weather-icon style of the reference blue wind icon and golden sun icon in the conversation; clean coherent silhouette legible at 24 to 30 pixels. Centered square composition with minimal transparent padding. True transparent background, no shadow plate, no badge, no text, no numbers, no percent sign, no border, no other objects. A polished detailed blue glass water drop, not a flat placeholder. Provide the generated image as a project asset with local path if available.'


def main():
    with Image.open(SOURCE) as original:
        if original.mode != 'RGBA' or original.getextrema()[3][0] != 0:
            raise ValueError('Valódi átlátszó RGBA eredeti szükséges')
        original = original.copy()

    destinations = [ROOT / 'main/assets/png/weather_humidity.png']
    for definition in sorted((ROOT / 'templates').glob('*/template.json')):
        if '"humidity"' in definition.read_text():
            destinations.append(definition.parent / 'weather_humidity.png')
    manifest_path = ROOT / 'assets/template-manifest.json'
    manifest = {item['asset']: item for item in json.loads(manifest_path.read_text())}
    for target in destinations:
        size = 28 if target.parent == ROOT / 'main/assets/png' else 30
        icon = ImageOps.contain(original, (size, size), Image.Resampling.LANCZOS)
        output = Image.new('RGBA', (size, size))
        output.paste(icon, ((size - icon.width) // 2, (size - icon.height) // 2))
        output.save(target, optimize=True)
        relative = target.relative_to(ROOT).as_posix()
        entry = {
            'asset': relative, 'generator': f'built-in image_gen; prepare_humidity_icon.py, {size}x{size}',
            'prompt': PROMPT, 'original_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
            'sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        }
        if target.parent == ROOT / 'main/assets/png':
            metadata = ROOT / 'assets/source/weather_humidity.json'
            metadata.write_text(json.dumps(entry, ensure_ascii=False, indent=2) + '\n')
            manifest.pop(relative, None)
        else:
            manifest[relative] = entry
        print(relative, target.stat().st_size)
    manifest_path.write_text(json.dumps([manifest[k] for k in sorted(manifest)],
                                       ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
