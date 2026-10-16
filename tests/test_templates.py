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
    assert solar['version'] >= 12
    # A tegnapi részleges összevetés tilde (~) jele minden UI-betűben jelen legyen.
    # Az előző U+2248 (≈) szimbólum hiányzott, ezért üres négyzetet rajzolt LVGL.
    for size in (12, 14, 16, 18, 20, 22, 28):
        font = (ROOT/f'main/fonts/app_font_{size}.c').read_text(encoding='utf-8')
        assert '/* U+007E */' in font, f'missing tilde glyph in {size}px font'
    assert len(solar['light']['elements']) <= 96
    for mode in ('dark','light'):
        elements=solar[mode]['elements']
        cards=[e for e in elements if e['type']=='box' and e['y']>=650]
        assert len(cards)==4
        assert [e['x'] for e in cards]==sorted(e['x'] for e in cards)
        assert len({(e['y'],e['w'],e['h']) for e in cards})==1
        assert all(e['h']<=112 and e['y']+e['h']<=800 for e in cards)
        assert {e['source'] for e in elements if e['type']=='value'} >= {
            'solar_savings_day','solar_value_today','consumption_today','production_today',
            'savings_yesterday_pct','production_value_yesterday_pct',
            'consumption_yesterday_pct',
            'battery_summary','production_yesterday_pct'}
        chips=[e for e in elements if e['type']=='box' and e['x']<930 and e['y']<300]
        assert len(chips)==3 and all(e['h']<=60 and e['opacity']<230 for e in chips)
        assert not any(e['type']=='box' and e['w']==1280 and e['y']==0 for e in elements)
        discs=[e for e in elements if e['type']=='solar_disc']
        assert len(discs)==1
        assert len(discs[0]['day_path'])==len(discs[0]['night_path'])==21
        assert not any(e['type']=='solar_arc' for e in elements)
        assert all(e['dot_size']<=6 for e in elements if e['type']=='flow')
        assert len([e for e in elements if e['type']=='flow' and e.get('reverse')])==2
        flow_nodes=[e for e in elements if e['type']=='flow']
        assert len(flow_nodes)==3
        assert all(e['radius']==3 and e['active_opacity']==250 and
                   e['inactive_opacity']==184 and e['inactive_glow_opacity']==28 for e in flow_nodes)
        assert all(e['inactive_opacity']<e['active_opacity']-50 for e in flow_nodes)
        battery_flow=next(e for e in flow_nodes if e['source']=='battery')
        assert battery_flow['curve']=='polyline' and battery_flow['reverse']
        assert battery_flow['x']==1016 and battery_flow['y']==472
        assert battery_flow['points']==[{'x':0,'y':1},{'x':22,'y':1},{'x':22,'y':88}]
        pv_icon=next(e for e in elements if e['type']=='image' and e.get('file')=='pv.png')
        home_icon=next(e for e in elements if e['type']=='image' and e.get('file')=='home.png')
        start_x=battery_flow['x']+battery_flow['points'][0]['x']
        corner_x=battery_flow['x']+battery_flow['points'][1]['x']
        assert start_x==pv_icon['x']+pv_icon['w']+4
        assert start_x<corner_x<home_icon['x']
        assert battery_flow['points'][0]['y']==battery_flow['points'][1]['y']
        assert battery_flow['points'][1]['x']==battery_flow['points'][2]['x']
        assert battery_flow['y']+battery_flow['points'][-1]['y']==560
        assert battery_flow['positive_color']!=battery_flow['negative_color']
        sunrise=next(e for e in elements if e['type']=='box' and e['y']==236 and e['x']<200)
        sunset=next(e for e in elements if e['type']=='box' and e['y']==236 and e['x']>800)
        assert sunrise['x']==26 and sunrise['x']+sunrise['w']<170
        assert sunset['x']==821 and sunset['x']+sunset['w']<936
        therm=next(e for e in elements if e['type']=='image' and e.get('file')=='battery_temp.png')
        band=next(e for e in elements if e['type']=='box' and e['x']==951 and e['y']==562)
        separators=[e for e in elements if e['type']=='box' and e.get('w')==2 and e.get('h')==4 and
                    e.get('background')==band['outline_color']]
        assert len(separators)==7
        assert all(e['x']<therm['x'] and band['y']<e['y']<band['y']+band['h'] for e in separators)
        assert len(elements)==94
        power=next(e for e in elements if e.get('type')=='value' and e.get('source')=='battery')
        assert power['font']==12 and power['x']==1093 and power['w']==51
        assert power['positive_color']!=power['negative_color']
        bar=next(e for e in elements if e['type']=='bar' and e['source']=='soc')
        assert (band['x'],band['y'],band['w'],band['h'])==(951,562,307,78)
        assert bar['y']>=626 and bar['y']+bar['h']<band['y']+band['h']
        assert bar['x']==1012 and bar['w']==129 and bar['h']==6
        for source,label in (('pv','Napelem'),('home','Háztartás'),('grid','grid_direction')):
            value=next(e for e in elements if e['type']=='value' and e['source']==source)
            caption=next(e for e in elements if e.get('text')==label or e.get('source')==label)
            assert value['x']*2+value['w']==caption['x']*2+caption['w']
            assert caption['align']=='center'
        assert not any(e.get('source') in ('export_today','export_yesterday_pct') for e in elements)
        assert next(e for e in elements if e['type']=='image' and e['x']==978)['file']=='pv.png'
        assert any(e.get('text')=='Háztartásban elhasznált energia' for e in elements)
        assert any(e.get('text')=='Napelemekből nyert energia' for e in elements)
        assert any(e.get('text')=='Helyben elhasznált és tárolt' for e in elements)
        assert any(e.get('text')=='Hálózatba visszatáplált' for e in elements)
        assert {e.get('background') for e in elements if e['type']=='box' and e['w']==e['h']==6}=={'#23B779','#FFCB35'}
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
    mismatched={
        item['asset']:hashlib.sha256((ROOT/item['asset']).read_bytes()).hexdigest()
        for item in manifest
        if hashlib.sha256((ROOT/item['asset']).read_bytes()).hexdigest()!=item['sha256']
    }
    assert not mismatched, f'Outdated template-manifest.json SHA256 entries: {json.dumps(mismatched,ensure_ascii=False)}'
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
