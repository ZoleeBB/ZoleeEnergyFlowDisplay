#!/usr/bin/env python3
"""Export package previews, an icon sheet and an animation from actual headless LVGL renders.

Run ui_preview first with ZOLEE_TEMPLATE_ROOT. Requires Pillow; images are not
mockups. Usage: prepare_template_previews.py .preview-images
"""
from pathlib import Path
from io import BytesIO
import sys
import json
import hashlib
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/previews'


def save_image(image,path,**options):
    """Encode completely in memory before atomically replacing a checked image."""
    output=BytesIO()
    image.save(output,format='GIF' if path.suffix.lower()=='.gif' else 'PNG',**options)
    data=output.getvalue()
    with Image.open(BytesIO(data)) as check:
        check.load()
    temporary=path.with_name('.'+path.name+'.tmp')
    try:
        temporary.write_bytes(data)
        if temporary.read_bytes()!=data:
            raise OSError(f'Incomplete image write: {path}')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main(source):
    source=Path(source);OUT.mkdir(parents=True,exist_ok=True)
    try:font=ImageFont.truetype('DejaVuSans.ttf',16)
    except OSError:font=ImageFont.load_default()
    packages=sorted(p for p in (ROOT/'templates').iterdir() if p.is_dir() and (p/'template.json').exists())
    icons=Image.new('RGBA',(950,1280),'#E6ECE9');idraw=ImageDraw.Draw(icons)
    for row,package in enumerate(packages):
        name=json.loads((package/'template.json').read_text())['name']
        for col,theme in enumerate(('light','dark')):
            with Image.open(source/f'template-{package.name}-{theme}.ppm') as frame:
                # Kis, teljes kijelzőkép: a web nem dekódolja a nagy fotóhátteret.
                target=package/f'preview-{theme}.png'
                save_image(frame.convert('RGB').resize((480,300),Image.Resampling.LANCZOS),target,optimize=True)
        if row>=7:continue
        idraw.text((12,row*150+55),name,font=font,fill='#19352F')
        for col,name in enumerate(('flow_solar','flow_house','flow_grid','flow_battery','flow_hub')):
            asset=package/(name+'.png')
            if asset.exists():
                with Image.open(asset) as icon:icons.alpha_composite(icon,(235+col*140,row*150+8))
    idraw.text((12,1080),'Saját időjárás:',font=font,fill='#19352F')
    for col,name in enumerate(('sunny','partly_cloudy','cloudy','rain','snow','night')):
        with Image.open(ROOT/'templates/05-calm-home'/('weather_'+name+'.png')) as icon:
            icons.alpha_composite(icon.resize((110,110)),(235+col*116,1070))
        idraw.text((235+col*116,1190),name.replace('partly_cloudy','partly'),font=font,fill='#19352F')
    save_image(icons.convert('RGB'),OUT/'template-icons.png',optimize=True)
    frames=[]
    for path in sorted(source.glob('live-home-flow-*.ppm')):
        with Image.open(path) as image:
            frames.append(image.crop((0,112,1280,650)).resize((640,269),Image.Resampling.LANCZOS).quantize(colors=96))
    if frames:save_image(frames[0],OUT/'live-home-flow.gif',save_all=True,append_images=frames[1:],duration=120,loop=0)
    with Image.open(source/'diagnostics-common.ppm') as frame:save_image(frame,OUT/'diagnostics.png',optimize=True)
    manifest_path=ROOT/'assets/template-manifest.json'
    manifest=[item for item in json.loads(manifest_path.read_text())
              if Path(item['asset']).name not in ('preview-light.png','preview-dark.png')]
    for package in packages:
        for mode in ('light','dark'):
            path=package/f'preview-{mode}.png'
            manifest.append({'asset':path.relative_to(ROOT).as_posix(),
                             'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                             'generator':'LVGL simulator screenshot, 480x300'})
    manifest.sort(key=lambda item:item['asset'])
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    print('Exported actual LVGL template previews, own icon sets and curved flow animation')


if __name__=='__main__':main(sys.argv[1] if len(sys.argv)>1 else '.preview-images')
