#!/usr/bin/env python3
"""Exercise the actual C installer against valid packages and hostile/corrupt input."""
import hashlib
import copy
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='zolee-template-test-') as tmp:
    tmp=Path(tmp)
    subprocess.run(['cc','-std=gnu11','-shared','-fPIC','-g','-O1','-Wall','-Wextra','-Werror',
                    '-Wno-misleading-indentation','-Werror=frame-larger-than=8192','-fsanitize=undefined',
                    '-fno-sanitize-recover=all','-DAPP_TEMPLATE_DESKTOP','-Isimulator/compat','-Itests/stubs','-Imain',
                    'main/app_templates.c','main/app_template_zip.c','-lz','-lm','-l:libcjson.so.1',
                    '-o',str(tmp/'templates.so')],cwd=ROOT,check=True)
    lib=C.CDLL(str(tmp/'templates.so'))
    lib.app_templates_validate.argtypes=[C.c_char_p,C.c_void_p,C.c_char_p]
    lib.app_templates_validate.restype=C.c_bool
    lib.app_templates_probe.argtypes=[C.c_char_p,C.POINTER(C.c_void_p),C.c_char_p]
    lib.cJSON_Delete.argtypes=[C.c_void_p]
    lib.app_templates_install.argtypes=[C.c_char_p,C.c_char_p]
    lib.app_templates_select.argtypes=[C.c_char_p]
    lib.app_templates_delete.argtypes=[C.c_char_p,C.c_char_p]
    lib.app_templates_list.argtypes=[C.c_void_p,C.c_uint,C.c_char_p]
    lib.app_templates_load_pending.argtypes=[C.c_void_p]
    lib.app_templates_load_pending.restype=C.c_bool
    lib.app_templates_view_free.argtypes=[C.c_void_p]
    error=C.create_string_buffer(160)
    # A jóváhagyott Nap pálya: a két üzemmód azonos elrendezés, kisméretű
    # nap-adat panelek és négy (méretváltozás nélküli) alsó összegzőkártya.
    solar=json.loads((ROOT/'templates/09-solar-journey/template.json').read_text(encoding='utf-8'))
    assert solar['version'] >= 5
    assert len(solar['light']['elements']) <= 96
    for mode in ('dark','light'):
        elements=solar[mode]['elements']
        cards=[e for e in elements if e['type']=='box' and e['y']>=650]
        assert len(cards)==4
        assert [e['x'] for e in cards]==sorted(e['x'] for e in cards)
        assert len({(e['y'],e['w'],e['h']) for e in cards})==1
        assert all(e['h']<=112 and e['y']+e['h']<=800 for e in cards)
        assert {e['source'] for e in elements if e['type']=='value'} >= {
            'solar_savings_day','solar_value_today','consumption_today','export_today',
            'savings_yesterday_pct','production_value_yesterday_pct',
            'consumption_yesterday_pct','export_yesterday_pct',
            'battery_summary','production_yesterday_pct'}
        chips=[e for e in elements if e['type']=='box' and e['x']<930 and e['y']<300]
        assert len(chips)==3 and all(e['h']<=60 and e['opacity']<230 for e in chips)
        assert not any(e['type']=='box' and e['w']==1280 and e['y']==0 for e in elements)
    # A két rétegsor valóban fedésben legyen; csak tónus és asset térhet el.
    geometric=lambda scene:[(e['type'],e['x'],e['y'],e['w'],e['h']) for e in scene['elements']]
    assert geometric(solar['light'])==geometric(solar['dark'])
    for package in sorted((ROOT/'templates').iterdir()):
        assert lib.app_templates_validate(os.fsencode(package),None,error), (package.name,error.value)
    print('PASS: all installed packages, both themes, references, PNG CRC/decompression and bounds')
    builtin={hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'main/assets/png').glob('*.png')}
    seen=set()
    for package in sorted((ROOT/'templates').iterdir()):
        for asset in package.glob('*.png'):
            if asset.name in ('day.png','night.png','background-day.png','background-night.png'):continue
            digest=hashlib.sha256(asset.read_bytes()).hexdigest()
            assert digest not in builtin, f'{asset}: copied builtin icon'
            seen.add(digest)
    # Checked-in PNGs must be used by a current scene, not historical leftovers.
    def referenced(value):
        if isinstance(value, str): return {value} if value.endswith('.png') else set()
        if isinstance(value, dict): return set().union(*(referenced(v) for v in value.values()))
        if isinstance(value, list): return set().union(*(referenced(v) for v in value))
        return set()
    for package in (ROOT/'templates').iterdir():
        if (package/'template.json').is_file():
            refs = referenced(json.loads((package/'template.json').read_text()))
            assert {p.name for p in package.glob('*.png')} == refs, package.name
    manifest = json.loads((ROOT/'assets/template-manifest.json').read_text())
    assert {item['asset'] for item in manifest} == {p.relative_to(ROOT).as_posix() for p in (ROOT/'templates').glob('*/*.png')}
    for item in manifest:
        assert hashlib.sha256((ROOT/item['asset']).read_bytes()).hexdigest() == item['sha256']
    print('PASS: generated template icons, no copied builtin assets')
    sd=tmp/'sd';sd.mkdir();os.environ['ZOLEE_TEMPLATE_ROOT']=str(sd)
    assert lib.app_templates_init()==0
    lib.app_templates_step_available.argtypes=[C.c_int,C.c_bool]
    # Nincs napelemes pozícióadat: az új téma átugrandó. A közös
    # függvény működése üres és normál katalógus mellett is biztonságos.
    lib.app_templates_step_available(1, False)
    def active():
        name=C.create_string_buffer(33);n=lib.app_templates_list(None,16,name)
        return n,name.value.decode()
    def archive(name,document=None,extra=None,compression=zipfile.ZIP_DEFLATED):
        path=tmp/(name+'.zip')
        with zipfile.ZipFile(path,'w',compression) as z:
            if document is not None:z.writestr('template.json',json.dumps(document).encode())
            for key,value in (extra or {}).items():z.writestr(key,value)
        return path
    empty=dict(background='#FFFFFF',elements=[])
    doc=dict(schema=1,id='test-layout',name='Test',version=1,light=empty,dark=empty)
    good=archive('good',doc)
    report=C.c_void_p();before=active()
    assert lib.app_templates_probe(os.fsencode(good),C.byref(report),error)==0,error.value
    assert report.value and active()==before and not (sd/'test-layout').exists() and not (sd/'.check').exists()
    lib.cJSON_Delete(report)
    assert lib.app_templates_install(os.fsencode(good),error)==0,error.value
    assert active()==(2,'builtin')
    assert lib.app_templates_select(b'test-layout')==0
    # Opaque aligned view storage is deliberately larger than the public struct.
    view=(C.c_uint64*1024)()
    assert lib.app_templates_load_pending(view)
    assert (sd/'selection.txt').read_text()=='test-layout'
    lib.app_templates_view_free(view)
    assert lib.app_templates_init()==0 and active()==(2,'test-layout')
    # Ugyanaz az azonosító most szándékosan, biztonsági mentéssel frissül.
    updated=copy.deepcopy(doc);updated['version']=2;updated['name']='Frissített nézet'
    upgraded=archive('updated',updated)
    report=C.c_void_p()
    assert lib.app_templates_probe(os.fsencode(upgraded),C.byref(report),error)==0,error.value
    lib.cJSON_Delete(report)
    assert lib.app_templates_install(os.fsencode(upgraded),error)==0,error.value
    assert active()==(2,'test-layout')
    assert json.loads((sd/'test-layout/template.json').read_text())['version']==2
    assert not (sd/'.replace').exists()
    assert lib.app_templates_load_pending(view)
    lib.app_templates_view_free(view)
    # A hibás új csomag soha nem változtathatja meg a meglévőt.
    broken=archive('broken',dict(updated,schema=99))
    assert lib.app_templates_install(os.fsencode(broken),error)!=0
    assert json.loads((sd/'test-layout/template.json').read_text())['version']==2
    for field,value in [('source','unknown'),('x',1280),('w',-1),('font',13),('shadow','true')]:
        bad=copy.deepcopy(doc);bad['id']='invalid'
        element=dict(type='value',x=0,y=0,w=100,h=40,source='pv',font=20)
        element[field]=value;bad['light']['elements']=[element]
        assert lib.app_templates_install(os.fsencode(archive('bad-schema',bad)),error)!=0,(field,error.value)
    # Curve controls are validated before the runtime samples them.
    flow=dict(type='flow',source='grid',x=0,y=0,w=200,h=200,curve='bezier',
              points=[dict(x=0,y=0),dict(x=100,y=0),dict(x=100,y=199),dict(x=199,y=199)])
    curved=copy.deepcopy(doc);curved['id']='curved';curved['light']['elements']=[flow]
    assert lib.app_templates_install(os.fsencode(archive('curved',curved)),error)==0,error.value
    assert lib.app_templates_delete(b'curved',error)==0
    two=copy.deepcopy(curved);two['light']['elements'][0]['points']+=flow['points'][1:]
    assert lib.app_templates_install(os.fsencode(archive('two-curves',two)),error)==0,error.value
    assert lib.app_templates_delete(b'curved',error)==0
    for curve,points in [('spline',flow['points']),('bezier',flow['points'][:3]),('bezier',flow['points']*2)]:
        bad=copy.deepcopy(curved);bad['light']['elements'][0].update(curve=curve,points=points)
        assert lib.app_templates_install(os.fsencode(archive('bad-curve',bad)),error)!=0
    bad=copy.deepcopy(curved);bad['light']['elements'][0]['points'][1]['x']=200
    assert lib.app_templates_install(os.fsencode(archive('outside-curve',bad)),error)!=0
    # All six custom weather states must exist; no silent builtin fallback.
    weather=dict(type='weather',x=0,y=0,w=128,h=128,files={k:'missing.png' for k in
                  ('sunny','partly_cloudy','cloudy','rain','snow','night')})
    bad=copy.deepcopy(doc);bad['id']='weather';bad['light']['elements']=[weather]
    assert lib.app_templates_install(os.fsencode(archive('missing-weather',bad)),error)!=0
    del weather['files']['night']
    assert lib.app_templates_install(os.fsencode(archive('incomplete-weather',bad)),error)!=0
    for name,data in [('../escaped.png',b'x'),('/absolute.png',b'x'),('nested/a.png',b'x'),
                      ('UPPER.png',b'x'),('broken.png',b'not png'),('huge.png',b'x'*(2*1024*1024+1))]:
        assert lib.app_templates_install(os.fsencode(archive('bad-path',dict(doc,id='invalid'),{name:data})),error)!=0,name
    # A valid PNG with corrupt compressed payload or CRC must not pass schema validation.
    png=bytearray((ROOT/'main/assets/png/flow_house.png').read_bytes());png[80]^=1
    assert lib.app_templates_install(os.fsencode(archive('crc',dict(doc,id='invalid'),{'house.png':png})),error)!=0
    symlink=tmp/'symlink.zip'
    with zipfile.ZipFile(symlink,'w') as z:
        z.writestr('template.json',json.dumps(dict(doc,id='invalid')))
        entry=zipfile.ZipInfo('house.png');entry.create_system=3;entry.external_attr=0o120777<<16
        z.writestr(entry,'../../outside')
    assert lib.app_templates_install(os.fsencode(symlink),error)!=0
    truncated=tmp/'truncated.zip';truncated.write_bytes(good.read_bytes()[:-10])
    assert lib.app_templates_probe(os.fsencode(truncated),C.byref(report),error)!=0
    assert not report.value and active()==(2,'test-layout') and not (sd/'.check').exists()
    assert lib.app_templates_install(os.fsencode(truncated),error)!=0
    duplicate=tmp/'duplicate.zip'
    with zipfile.ZipFile(duplicate,'w') as z:
        z.writestr('template.json',json.dumps(dict(doc,id='invalid')))
        z.writestr('template.json',json.dumps(dict(doc,id='invalid')))
    assert lib.app_templates_install(os.fsencode(duplicate),error)!=0
    assert active()==(2,'test-layout') and not (tmp/'escaped.png').exists()
    assert not (sd/'.install').exists()
    assert lib.app_templates_delete(b'builtin',error)!=0
    assert lib.app_templates_delete(b'test-layout',error)==0 and active()==(1,'builtin')
    assert lib.app_templates_load_pending(view);lib.app_templates_view_free(view)
    assert lib.app_templates_init()==0 and active()==(1,'builtin')
    # Stored ZIP and missing selected package fallback.
    assert lib.app_templates_install(os.fsencode(archive('stored',doc,compression=zipfile.ZIP_STORED)),error)==0
    assert lib.app_templates_select(b'test-layout')==0
    shutil.rmtree(sd/'test-layout')
    assert lib.app_templates_load_pending(view);lib.app_templates_view_free(view)
    assert active()[1]=='builtin'
    print('PASS: install, transactional update, active reload, persistence, delete, builtin protection, fallback, stored/deflate ZIP, rejected unsafe/corrupt packages')
