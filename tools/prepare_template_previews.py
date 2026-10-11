#!/usr/bin/env python3
"""Export QA contact sheets and an animation from actual headless LVGL renders.

Run ui_preview first with ZOLEE_TEMPLATE_ROOT. Requires Pillow; images are not
mockups. Usage: prepare_template_previews.py .preview-images
"""
from pathlib import Path
import sys
import json
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/previews'


def main(source):
    source=Path(source);OUT.mkdir(parents=True,exist_ok=True)
    try:font=ImageFont.truetype('DejaVuSans.ttf',16)
    except OSError:font=ImageFont.load_default()
    packages=sorted(p for p in (ROOT/'templates').iterdir() if p.is_dir() and (p/'template.json').exists())
    sheet=Image.new('RGB',(800,len(packages)*280),'#DCE3E4');draw=ImageDraw.Draw(sheet)
    icons=Image.new('RGBA',(950,1280),'#E6ECE9');idraw=ImageDraw.Draw(icons)
    for row,package in enumerate(packages):
        name=json.loads((package/'template.json').read_text())['name']
        for col,theme in enumerate(('light','dark')):
            with Image.open(source/f'template-{package.name}-{theme}.ppm') as frame:
                sheet.paste(frame.resize((400,250),Image.Resampling.LANCZOS),(col*400,row*280+30))
            draw.text((col*400+10,row*280+5),name+(' · nappal' if col==0 else ' · éjjel'),font=font,fill='#19352F')
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
    sheet.save(OUT/'templates.png',optimize=True);icons.convert('RGB').save(OUT/'template-icons.png',optimize=True)
    frames=[]
    for path in sorted(source.glob('live-home-flow-*.ppm')):
        with Image.open(path) as image:
            frames.append(image.crop((0,112,1280,650)).resize((640,269),Image.Resampling.LANCZOS).quantize(colors=96))
    if frames:frames[0].save(OUT/'live-home-flow.gif',save_all=True,append_images=frames[1:],duration=120,loop=0)
    with Image.open(source/'diagnostics-common.ppm') as frame:frame.save(OUT/'diagnostics.png',optimize=True)
    print('Exported actual LVGL template previews, own icon sets and curved flow animation')


if __name__=='__main__':main(sys.argv[1] if len(sys.argv)>1 else '.preview-images')
