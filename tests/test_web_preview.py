#!/usr/bin/env python3
import json,os,shutil,struct,subprocess,tempfile,zipfile,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
# A webes JavaScript változtatása ne maradhasson ki a firmware-be égetett headerből.
subprocess.run([os.sys.executable, str(ROOT/'tools/prepare_web.py'), '--check'], check=True)
node=shutil.which('node')
if not node:
    print('SKIP: browser ZIP test requires Node 24 (CI installs it)')
else:
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+b'\xff'*8+b'\0'+b'\xff'*8))+chunk(b'IEND',b'')
    scene={'background':'#FFFFFF','elements':[{'type':'image','x':0,'y':0,'w':2,'h':2,'file':'image.png'}]}
    doc={'schema':1,'id':'test-layout','name':'Test','version':1,'light':scene,'dark':scene}
    with tempfile.TemporaryDirectory() as directory:
        paths=[]
        for i,compression in enumerate((zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED)):
            path=Path(directory)/f'good{i}.zip'
            with zipfile.ZipFile(path,'w',compression) as z:z.writestr('template.json',json.dumps(doc));z.writestr('image.png',png)
            paths.append(path)
        for name in ('../image.png','image.png'):
            path=Path(directory)/('path.zip' if name.startswith('..') else 'duplicate.zip')
            with zipfile.ZipFile(path,'w') as z:
                z.writestr('template.json',json.dumps(doc));z.writestr(name,png)
                if name=='image.png':z.writestr(name,png)
            paths.append(path)
        truncated=Path(directory)/'truncated.zip';truncated.write_bytes(paths[0].read_bytes()[:-5]);paths.append(truncated)
        bad=Path(directory)/'crc.zip';data=bytearray(paths[0].read_bytes());data[50]^=1;bad.write_bytes(data);paths.append(bad)
        sd=Path(directory)/'sd';sd.mkdir()
        binary=Path(directory)/'test_web'
        subprocess.run(['cc','-std=gnu11','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                        '-Werror=frame-larger-than=8192','-fsanitize=undefined','-fno-sanitize-recover=all',
                        '-DAPP_TEMPLATE_DESKTOP','-Isimulator/compat','-Itests/stubs','-Imain',
                        'tests/test_template_web.c','main/app_templates.c','main/app_template_zip.c',
                        '-lz','-lm','-l:libcjson.so.1','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary),str(paths[0]),str(sd)],check=True)
        # Valódi, minden eredeti PNG-t tartalmazó kilencedik sablon a böngészős validátorhoz.
        solar_folder=ROOT/'templates/09-solar-journey'
        solar_zip=Path(directory)/'solar.zip'
        with zipfile.ZipFile(solar_zip,'w',zipfile.ZIP_DEFLATED) as archive:
            for source in sorted(solar_folder.iterdir()):
                if source.is_file():archive.write(source,source.name)
        subprocess.run([node,str(ROOT/'tests/test_web_preview.js'),
                        *map(str,paths[:2]),str(solar_zip),*map(str,paths[2:])],
                       cwd=ROOT,check=True)
