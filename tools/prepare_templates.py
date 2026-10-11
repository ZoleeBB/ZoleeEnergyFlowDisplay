#!/usr/bin/env python3
"""Build seven editable JSON layouts and flat, independently installable ZIPs.

House backgrounds are checked-in generated art. All data, lines, dots, charts and
labels are real runtime widgets, never baked into screenshots.
"""
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / 'templates'
GREEN, ORANGE, BLUE, RED = '#36AE7C', '#EA9C3B', '#559CDE', '#E66B6B'


class Scene:
    def __init__(self, dark, background=None):
        self.dark = dark
        self.bg = background or ('#111F2C' if dark else '#F0F5F3')
        self.card = '#1B2C3B' if dark else '#FFFFFF'
        self.text = '#EDF4F8' if dark else '#19352F'
        self.muted = '#A2B5BE' if dark else '#657A75'
        self.elements = []

    def add(self, element_type, x, y, w, h, **kw):
        self.elements.append(dict(type=element_type, x=x, y=y, w=w, h=h, **kw))

    def box(self, x, y, w, h, **kw):
        self.add('box', x, y, w, h, background=kw.pop('background', self.card), radius=kw.pop('radius', 22), **kw)

    def label(self, text, x, y, w=250, font=20, color=None, en=None, **kw):
        self.add('label', x, y, w, font+10, text=text, text_en=en or text, font=font, color=color or self.text, **kw)

    def value(self, source, x, y, w=250, font=28, color=None, **kw):
        self.add('value', x, y, w, font+12, source=source, font=font, color=color or self.text, **kw)

    def icon(self, name, x, y, size=100):
        self.add('image', x, y, size, size, file=name+'.png')

    def flow(self, source, points, color, thickness=5, curve='polyline'):
        x, y = min(p[0] for p in points), min(p[1] for p in points)
        w, h = max(p[0] for p in points)-x+1, max(p[1] for p in points)-y+1
        self.add('flow', x, y, w, h, source=source, color=color, radius=thickness, curve=curve,
                 points=[dict(x=a-x, y=b-y) for a, b in points])

    def header(self, title, subtitle):
        self.label('Zolee', 32, 24, 150, 28)
        self.label('Energy Folow Monitor', 32, 59, 250, 16, self.muted)
        self.label(title, 338, 24, 500, 28)
        self.label(subtitle, 338, 60, 600, 16, self.muted)
        self.value('clock', 1080, 20, 175, 48)
        self.value('date', 1090, 75, 155, 16, self.muted)

    def daily(self, y=650):
        for i, (label, source, color) in enumerate([
            ('Termelés ma', 'production_today', GREEN), ('Fogyasztás ma', 'consumption_today', ORANGE),
            ('Össz. töltés ma', 'charge_today', BLUE), ('Össz. leadás ma', 'discharge_today', BLUE)]):
            x = 32+i*308
            self.box(x, y, 292, 106)
            self.label(label, x+20, y+16, 260, 16, self.muted)
            self.value(source, x+20, y+46, 262, 28, color)

    def document(self):
        assert len(self.elements) <= 96
        return dict(background=self.bg, elements=self.elements)


def house(dark):
    s = Scene(dark)
    s.add('image', 0, 0, 1280, 800, file='night.png' if dark else 'day.png')
    s.box(20, 16, 1240, 96, opacity=230)
    s.header('Élő otthon', 'Az otthonod energiája, valós időben')
    # Live links: grid->house, solar->house, battery->house (reverse while charging).
    s.flow('grid', [(100,316),(330,316),(330,526),(650,526)], RED, curve='bezier')
    s.flow('pv', [(640,300),(600,380),(700,425),(650,526)], GREEN, curve='bezier')
    s.flow('battery', [(1050,530),(930,595),(800,475),(650,526)], BLUE, curve='bezier')
    for x, y, title, source, color, extra in [
        (32, 160, 'Hálózat', 'grid', RED, 'grid_direction'),
        (480, 145, 'Napelemek', 'pv', GREEN, None),
        (980, 350, 'Akkumulátor', 'battery', BLUE, 'battery_direction'),
        (478, 540, 'Otthon', 'home', GREEN, None)]:
        s.box(x, y, 270, 110, opacity=237)
        s.icon({'grid':'flow_grid','pv':'flow_solar','battery':'flow_battery','home':'flow_house'}[source], x+9,y+20,54)
        s.label(title, x+73, y+12, 190, 16, s.muted)
        s.value(source, x+73, y+37, 190, 28, color)
        if extra: s.value(extra, x+18, y+78, 235, 12, s.muted)
    s.box(980, 476, 270, 86, opacity=237)
    s.value('soc', 1000, 484, 220, 28, BLUE)
    s.add('bar', 1000, 532, 225, 8, source='soc', color=BLUE)
    s.daily(660)
    return s


def paths(dark):
    s = Scene(dark, '#13272A' if dark else '#F4F7F1')
    s.header('Energiautak', 'Honnan érkezik és merre halad az energia?')
    s.box(32, 126, 1216, 496)
    s.flow('pv', [(244, 240), (440, 240), (620, 378)], GREEN, 28)
    s.flow('grid', [(244, 475), (440, 475), (620, 378)], ORANGE, 28)
    s.flow('home', [(650, 378), (840, 235), (1020, 235)], GREEN, 28)
    s.flow('battery', [(1020, 475), (840, 475), (650, 378)], BLUE, 28)
    for x, y, icon, title, source, color in [
        (70, 160, 'flow_solar', 'Napelemek', 'pv', GREEN),
        (70, 390, 'flow_grid', 'Hálózat', 'grid', ORANGE),
        (1000, 150, 'flow_house', 'Otthon', 'home', GREEN),
        (1000, 380, 'flow_battery', 'Tároló', 'battery', BLUE)]:
        s.icon(icon, x+30, y, 100)
        s.label(title, x, y+105, 205, 16, s.muted, align='center')
        s.value(source, x-5, y+135, 215, 28, color, align='center')
    s.box(530, 306, 220, 142, background=s.bg, radius=70)
    s.icon('flow_hub', 589, 309, 90)
    s.label('ENERGIA', 553, 405, 175, 16, s.muted, align='center')
    s.value('status', 440, 558, 400, 16, s.muted, align='center')
    s.daily()
    return s


def instruments(dark):
    s = Scene(dark, '#0B1525' if dark else '#EDF1F8')
    s.header('Műszerfal', 'Pillanatnyi teljesítmény / kW')
    for i, (name, source, color, icon) in enumerate([
        ('TERMELÉS', 'pv', GREEN, 'flow_solar'), ('FOGYASZTÁS', 'home', ORANGE, 'flow_house'),
        ('HÁLÓZAT', 'grid', RED, 'flow_grid'), ('AKKUMULÁTOR', 'battery', BLUE, 'flow_battery')]):
        x=32+i*308
        s.box(x, 136, 292, 395)
        s.add('gauge', x+27, 190, 238, 238, source=source, color=color, max=15)
        s.icon(icon, x+114, 215, 64)
        s.label(name, x+18, 163, 256, 16, s.muted, align='center')
        s.value(source, x+35, 296, 228, 28, color, align='center')
        s.label('0                    15 kW', x+40, 423, 220, 12, s.muted, align='center')
        if source in ('battery','grid'): s.value(source+'_direction', x+15, 471, 262, 16, s.muted, align='center')
    s.box(32, 551, 1216, 78)
    s.label('Tároló töltöttsége', 56, 577, 225, 20)
    s.add('bar', 299, 582, 430, 16, source='soc', color=BLUE)
    s.value('soc', 760, 572, 160, 28, BLUE)
    s.value('status', 910, 577, 300, 16, s.muted)
    s.daily()
    return s


def balance(dark):
    s = Scene(dark, '#152128' if dark else '#F3F6F8')
    s.header('Napi mérleg', 'Teljesítmény és energia, egyetlen áttekintésben')
    for row, kind, label in [(0,'production','Termelés'), (1,'consumption','Fogyasztás')]:
        y=120+row*265
        s.box(32,y,1216,247)
        s.icon('flow_solar' if row==0 else 'flow_house',50,y+5,43)
        s.label(label,104,y+15,190,20)
        colors=[GREEN,ORANGE,BLUE] if row==0 else [RED,GREEN,BLUE]
        labels=['Saját fogyasztás','Visszatáplálás','Tárolt'] if row==0 else ['Hálózat','Termelés','Tároló']
        for i,(name,color) in enumerate(zip(labels,colors)):
            s.label(name,700+i*177,y+19,173,16,color)
        s.add('chart',53,y+56,1160,174,kind=kind,color=s.muted)
    s.daily()
    s.icon('flow_grid',650,390,32)
    s.icon('flow_battery',1015,125,32)
    return s


def calm(dark):
    s = Scene(dark, '#242C29' if dark else '#F3F0E7')
    s.header('Nyugodt otthon', 'Csak ami igazán fontos')
    s.box(32,136,490,494,background='#303D36' if dark else '#E3EBDF',radius=30)
    s.value('clock',72,175,400,96,align='center')
    s.value('date',130,290,300,20,s.muted,align='center')
    s.add('weather',178,345,160,160,files={k:'weather_'+k+'.png' for k in ('sunny','partly_cloudy','cloudy','rain','snow','night')})
    s.value('temperature',88,520,220,48)
    s.label('Szél',345,517,140,16,s.muted)
    s.value('wind',345,550,150,28)
    for y,name,source,color,icon in [
        (136,'Az otthon fogyasztása','home',ORANGE,'flow_house'),
        (304,'A nap ereje','pv',GREEN,'flow_solar'),
        (472,'Energia a tárolóban','soc',BLUE,'flow_battery')]:
        s.box(542,y,706,158,radius=30)
        s.icon(icon,575,y+25,100)
        s.label(name,708,y+27,480,20,s.muted)
        s.value(source,708,y+70,480,48,color)
    s.box(32,650,1216,108,radius=30)
    s.icon('flow_grid',465,681,50)
    s.value('status',60,690,370,20)
    s.label('Termelés ma',550,670,300,16,s.muted)
    s.value('production_today',550,699,300,28,GREEN)
    s.label('Fogyasztás ma',925,670,290,16,s.muted)
    s.value('consumption_today',925,699,290,28,ORANGE)
    return s


def orbit(dark):
    s=Scene(dark,'#11142A' if dark else '#EFF2FB')
    s.card='#20243F' if dark else '#FFFFFF'
    s.header('Energiakör','Az energia négy szereplője, egy közös rendszerben')
    s.box(32,128,1216,502,radius=36)
    for source,points,color in [
        ('pv',[(197,210),(390,110),(540,140),(640,346)],GREEN),
        ('grid',[(197,460),(390,615),(565,610),(640,386)],RED),
        ('home',[(650,346),(760,165),(900,155),(1040,238)],ORANGE),
        ('battery',[(1040,512),(900,600),(740,575),(650,386)],BLUE)]:
        s.flow(source,points,color,8,curve='bezier')
    s.box(502,284,276,174,background=s.bg,radius=80)
    s.label('TÁROLÓ',538,308,204,16,s.muted,align='center')
    s.value('soc',528,342,225,48,BLUE,align='center')
    s.add('bar',549,412,182,8,source='soc',color=BLUE)
    for x,y,title,source,icon,color in [
        (81,150,'Napelemek','pv','flow_solar',GREEN),
        (81,399,'Hálózat','grid','flow_grid',RED),
        (978,150,'Otthon','home','flow_house',ORANGE),
        (978,399,'Akkumulátor','battery','flow_battery',BLUE)]:
        s.icon(icon,x+16,y,100)
        s.label(title,x-22,y+104,184,16,s.muted,align='center')
        s.value(source,x-32,y+135,204,28,color,align='center')
    s.daily()
    return s


def ledger(dark):
    s=Scene(dark,'#18292B' if dark else '#F6F2EA')
    s.card='#233A3B' if dark else '#FFFEFA'
    s.header('Energiafőkönyv','Pillanatnyi értékek és a mai energia egymás mellett')
    s.box(32,126,786,500,radius=16)
    s.label('ENERGIAFORRÁS',60,144,270,16,s.muted)
    s.label('MOST',377,144,210,16,s.muted)
    s.label('MA',603,144,190,16,s.muted)
    for i,(title,source,total,icon,color) in enumerate([
        ('Napelemek','pv','production_today','flow_solar',GREEN),
        ('Otthon','home','consumption_today','flow_house',ORANGE),
        ('Hálózat','grid','import_today','flow_grid',RED),
        ('Akkumulátor','battery','charge_today','flow_battery',BLUE)]):
        y=185+i*106
        s.box(48,y,754,98,background=s.bg,radius=12)
        s.icon(icon,64,y+16,64)
        s.label(title,146,y+16,230,20)
        if source in ('grid','battery'):s.value(source+'_direction',146,y+53,230,16,s.muted)
        s.value(source,377,y+28,215,28,color)
        s.value(total,603,y+28,190,28,color)
        if source in ('grid','battery'):s.label('Import ma' if source=='grid' else 'Töltés ma',603,y+65,190,12,s.muted)
    s.box(842,126,406,238,radius=16)
    s.label('Töltöttség',869,148,350,20,s.muted)
    s.value('soc',869,193,350,48,BLUE)
    s.add('bar',869,267,350,16,source='soc',color=BLUE)
    s.value('battery_temp',869,303,350,28)
    s.box(842,385,406,241,radius=16)
    s.label('Visszatáplálás ma',869,408,350,20,s.muted)
    s.value('export_today',869,447,350,48,ORANGE)
    s.label('Akku leadás ma',869,522,350,16,s.muted)
    s.value('discharge_today',869,553,350,28,BLUE)
    s.daily()
    return s


def main():
    for ident,name,builder in [('01-live-home','Élő otthon',house), ('02-energy-paths','Energiautak',paths),
                               ('03-instruments','Műszerfal',instruments), ('04-daily-balance','Napi mérleg',balance),
                               ('05-calm-home','Nyugodt otthon',calm),
                               ('06-energy-orbit','Energiakör',orbit),('07-energy-ledger','Energiafőkönyv',ledger)]:
        folder=PACKAGES/ident;folder.mkdir(parents=True,exist_ok=True)
        doc=dict(schema=1,id=ident,name=name,version=2,light=builder(False).document(),dark=builder(True).document())
        (folder/'template.json').write_text(json.dumps(doc,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        for theme in ('light','dark'):
            for element in doc[theme]['elements']:
                files=([element['file']] if element['type']=='image' else list(element.get('files',{}).values()))
                for filename in files:
                    if not (folder/filename).is_file():
                        raise FileNotFoundError(f'{ident}: saját asset hiányzik: {filename}')
        output=ROOT/'dist/templates';output.mkdir(parents=True,exist_ok=True)
        with zipfile.ZipFile(output/(ident+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
            for path in sorted(folder.iterdir()):
                if path.is_file():z.write(path,path.name)
        print(ident, 'light/dark:',len(doc['light']['elements']),len(doc['dark']['elements']))


if __name__=='__main__':main()
