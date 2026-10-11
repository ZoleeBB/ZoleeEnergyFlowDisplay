#!/usr/bin/env python3
"""Import generated transparent originals without repainting or changing their alpha.

Input: JSON list with template, name, path, prompt. Originals are retained at their
source paths. Runtime assets and portable prompt/sha provenance are saved in repo.
Requires Pillow (also used by the existing asset preparation tools).
"""
import hashlib
import json
from pathlib import Path
import re
import sys
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]


def main(manifest):
    output_manifest = ROOT/'assets/template-manifest.json'
    provenance = {item['asset']: item for item in json.loads(output_manifest.read_text(encoding='utf-8'))}
    for item in json.loads(Path(manifest).read_text(encoding='utf-8')):
        ident, name = item['template'], item['name']
        if not re.fullmatch(r'[a-z0-9_-]{1,32}', ident) or not re.fullmatch(r'[a-z0-9_-]{1,60}', name):
            raise ValueError('Unsafe asset name')
        source = Path(item['path'])
        with Image.open(source) as original:
            if original.mode != 'RGBA' or original.getextrema()[3][0] != 0:
                raise ValueError(f'{source}: actual transparent RGBA required')
            # Resolution conversion only: generated shape/colors/alpha remain intact.
            icon = ImageOps.contain(original, (128,128), Image.Resampling.LANCZOS)
            output = Image.new('RGBA', (128,128))
            output.paste(icon, ((128-icon.width)//2,(128-icon.height)//2))
            folder = ROOT/'templates'/ident
            folder.mkdir(parents=True,exist_ok=True)
            destination = folder/(name+'.png')
            output.save(destination,optimize=True)
        provenance[destination.relative_to(ROOT).as_posix()] = dict(asset=destination.relative_to(ROOT).as_posix(),
                               generator='built-in image_gen',prompt=item['prompt'],
                               original_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                               sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
        print(ident,name,destination.stat().st_size)
    output_manifest.write_text(
        json.dumps([provenance[key] for key in sorted(provenance) if (ROOT/key).is_file()],ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv)!=2:raise SystemExit('Usage: import_template_icons.py manifest.json')
    main(sys.argv[1])
