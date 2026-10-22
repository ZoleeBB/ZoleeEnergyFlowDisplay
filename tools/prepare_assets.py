#!/usr/bin/env python3
"""Package original AI artwork for LVGL; requires ImageMagick 6 or 7.

No screenshot extraction or illustration is performed here. Original artwork
is preserved in assets/source. The approved 4x2 icon sheet is separated by cell;
transparent padding is normalized and the artwork is uniformly downsampled.
"""
import json
import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/source'
OUTPUT = ROOT / 'main/assets/png'

def main():
    convert = shutil.which('magick') or shutil.which('convert')
    if not convert:
        raise SystemExit('ImageMagick is required')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    entries = json.loads((SOURCE / 'manifest.json').read_text())
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', nargs='+', choices=[entry['name'] for entry in entries])
    selected = parser.parse_args().only
    if selected:
        # Új ikon importálásakor a korábban jóváhagyott képek bájtjai változatlanok maradnak.
        entries = [entry for entry in entries if entry['name'] in selected]
    with tempfile.TemporaryDirectory() as directory:
        cell = Path(directory) / 'cell.png'
        for entry in entries:
            args = [convert, str(SOURCE / entry['source'])]
            if 'cell' in entry:
                x,y,w,h = entry['cell']
                args += ['-crop',f'{w}x{h}+{x}+{y}','+repage']
            subprocess.run(args + [str(cell)], check=True)
            # Alpha-only measurement avoids nearly invisible generated noise.
            # It does not replace or threshold the actual artwork alpha.
            geometry = subprocess.check_output([convert,str(cell),'-alpha','extract','-threshold','10%','-format','%@','info:'],text=True).strip()
            w,h,x,y = map(int,re.fullmatch(r'(\d+)x(\d+)\+(\d+)\+(\d+)',geometry).groups())
            # Retain antialiased edge pixels around the measured solid content.
            left,top=max(0,x-8),max(0,y-8)
            geometry=f'{w+x-left+8}x{h+y-top+8}+{left}+{top}'
            subprocess.run([convert,str(cell),'-crop',geometry,'+repage','-filter','Lanczos','-resize','112x112','-gravity','center','-background','none','-extent','128x128','-strip','PNG32:'+str(OUTPUT / (entry['name']+'.png'))],check=True)
    print(f'Prepared {len(entries)} transparent 128x128 PNG assets')

if __name__ == '__main__':
    main()
