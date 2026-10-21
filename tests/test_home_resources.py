#!/usr/bin/env python3
"""Valódi csomag és hibás variánsai, C-telepítővel és a webes JS-sel."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from prepare_home_resources import package_home_resources
with tempfile.TemporaryDirectory(prefix='home-resources-') as directory:
    folder=Path(directory)
    archive=package_home_resources(folder)
    with zipfile.ZipFile(archive) as source:
        original={name:source.read(name) for name in source.namelist()}
    invalid=[]
    for case in ('normal','missing','extra','missing-background','invalid-background'):
        files=original.copy();doc=json.loads(files['template.json'])
        if case=='normal':
            doc.pop('kind');doc['id']='test-layout';files['template.json']=json.dumps(doc).encode()
        elif case=='missing':files.pop('icon-washing-machine.png')
        elif case=='missing-background':files.pop('consumers-dark.png')
        elif case=='invalid-background':
            doc['consumer_backgrounds']['dark']='wrong.png';files['template.json']=json.dumps(doc).encode()
        else:files['extra.png']=files['icon-washing-machine.png']
        target=folder/(case+'.zip')
        with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as output:
            for name,body in files.items():output.writestr(name,body)
        invalid.append(str(target))
    legacy=original.copy();doc=json.loads(legacy['template.json'])
    doc['version']=1;doc.pop('consumer_backgrounds')
    legacy['template.json']=json.dumps(doc).encode()
    legacy.pop('consumers-light.png');legacy.pop('consumers-dark.png')
    legacy_path=folder/'legacy.zip'
    with zipfile.ZipFile(legacy_path,'w',zipfile.ZIP_DEFLATED) as output:
        for name,body in legacy.items():output.writestr(name,body)
    binary=str(folder/'test')
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror',
        '-Wno-misleading-indentation','-Werror=frame-larger-than=8192','-fsanitize=undefined',
        '-fno-sanitize-recover=all','-DAPP_TEMPLATE_DESKTOP','-Isimulator/compat','-Itests/stubs','-Imain',
        'tests/test_home_resources.c','main/app_templates.c','main/app_template_zip.c',
        '-lz','-lm','-l:libcjson.so.1','-o',binary],cwd=ROOT,check=True)
    sd=folder/'sd';sd.mkdir()
    subprocess.run([binary,str(archive),str(sd),*invalid,str(legacy_path)],check=True,timeout=30)
    subprocess.run(['node','tests/test_home_resources_web.js',str(archive),*invalid,str(legacy_path)],cwd=ROOT,check=True,timeout=30)
