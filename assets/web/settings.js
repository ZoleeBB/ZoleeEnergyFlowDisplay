'use strict';
const definitions=[
['Általános',[
['DataReceptMode','Adatfogadási mód','mode'],['project.title','Projekt neve'],['project.language','Nyelv','language'],['project.currency','Pénznem']]],
['Wi-Fi és hálózat',[
['wifi.ssid','SSID'],['wifi.password','Wi-Fi jelszó','secret'],['wifi.reconnect_seconds','Újracsatlakozás (másodperc)','integer'],
['wifi.network.use_static_ip','Statikus IP használata','bool'],['wifi.network.ip','IP cím'],['wifi.network.gateway','Átjáró'],['wifi.network.subnet','Alhálózati maszk'],['wifi.network.dns','DNS cím']]],
['NTP és idő',[
['time.ntp_server','NTP kiszolgáló'],['time.timezone','Időzóna'],['time.auto_dst','Automatikus nyári időszámítás','bool'],['time.utc_offset_minutes','UTC eltolás (perc)','integer'],
['time.startup_retry_seconds','Indulási újrapróbálkozás (másodperc)','integer'],['time.resync_hours','Újraszinkronizálás (óra)','integer'],['time.background_retry_minutes','Háttér újrapróbálkozás (perc)','integer']]],
['MQTT',[
['mqtt.broker','Broker cím'],['mqtt.port','Port','integer'],['mqtt.topic','Topic'],['mqtt.username','Felhasználónév'],
['mqtt.password','MQTT jelszó','secret'],['mqtt.data_timeout_seconds','Adat időtúllépés (másodperc)','integer'],['mqtt.reconnect_seconds','Újracsatlakozás (másodperc)','integer']]],
['HTTP adatfogadás', [['http.data_timeout_seconds','Adat időtúllépés (másodperc)','integer']]],
['Kijelző', [['display.day_brightness','Nappali fényerő (%)','integer'],['display.night_brightness','Éjszakai fényerő (%)','integer'],['display.rotation_degrees','Kijelző tájolása','rotation']]],
['Statisztika', [['statistics.save_interval_minutes','Mentési időköz (perc, maximum 15)','integer'],['statistics.max_integration_gap_seconds','Maximális mintaköz (másodperc)','integer']]],
['Energia és költség',[
['energy.battery_capacity_kwh','Akkumulátor maximális kapacitása (kWh, 0 = ismeretlen)','number'],['energy.electricity_purchase_price','Vételi ár','number'],['energy.feed_in_price','Visszatáplálási ár','number'],
['energy.co2_factor_kg_per_kwh','CO₂ tényező (kg/kWh)','number'],['energy.tree_absorption_kg_per_year','Fa elnyelés (kg/év)','number']]],
['Webes hozzáférés', [['ota.username','Adminisztrátori felhasználónév'],['ota.password','Adminisztrátori jelszó','secret']]]];
const form=document.getElementById('settings'),holder=document.getElementById('fields'),message=document.getElementById('message'),save=document.getElementById('save');
let state=null;
const fieldTypes=new Map();
const byPath=p=>document.getElementById(p);
const nestedGet=(obj,path)=>path.split('.').reduce((v,k)=>v?.[k],obj);
function nestedSet(obj,path,value){const a=path.split('.'),last=a.pop();const target=a.reduce((v,k)=>v[k],obj);target[last]=value;}
for(const [title,fields] of definitions){
 const section=document.createElement('section'),h=document.createElement('h2'),grid=document.createElement('div');h.textContent=title;grid.className='grid';
 for(const [path,labelText,type='text'] of fields){
  const label=document.createElement('label'),input=document.createElement(type==='mode'||type==='language'||type==='rotation'?'select':'input');
  label.textContent=labelText;input.id=path;fieldTypes.set(path,type);
  if(type==='bool')input.type='checkbox';
  else if(type==='integer'||type==='number'){input.type='number';input.step=type==='integer'?'1':'any';}
  else if(type==='secret'){input.type='password';input.autocomplete='new-password';input.placeholder='Változatlan jelszó';}
  else if(input.tagName==='INPUT')input.type='text';
  if(type==='mode'||type==='language'||type==='rotation')for(const [value,text] of (type==='mode'?[['mqtt','MQTT'],['http','HTTP'],['loxone','Loxone']]:type==='language'?[['hu','Magyar'],['en','Angol']]:[['270','270° – csatlakozók jobb oldalon'],['90','90° – csatlakozók bal oldalon']])){
   const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);
  }
  label.append(input);
  if(type==='secret'){const clear=document.createElement('label'),check=document.createElement('input');clear.className='clear';clear.textContent='Meglévő jelszó törlése';check.type='checkbox';check.id=path+'__clear';clear.prepend(check);label.append(clear);check.onchange=()=>{input.disabled=check.checked;if(check.checked)input.value='';};}
  grid.append(label);
 }
 section.append(h,grid);holder.append(section);
}
function fill(){for(const [path,type] of fieldTypes){const e=byPath(path),value=nestedGet(state,path);if(type==='bool')e.checked=!!value;else e.value=(value == null ? '' : value);if(type==='secret'){const clear=byPath(path+'__clear');clear.checked=false;e.disabled=false;}}}
function values(){const result=structuredClone(state);for(const [path,type] of fieldTypes){
 const input=byPath(path);let value;
 if(type==='bool')value=input.checked?1:0;
 else if(type==='integer'||type==='number'||type==='rotation')value=Number(input.value);
 else if(type==='secret')value=byPath(path+'__clear').checked?'':(input.value||null);
 else value=input.value;
 nestedSet(result,path,value);
 }return result;
}
async function load(){message.textContent='Beállítások betöltése…';try{const response=await fetch('/api/settings',{cache:'no-store'});if(!response.ok)throw Error(await response.text());state=await response.json();fill();message.textContent='';}catch(e){message.textContent='Betöltési hiba: '+e.message;save.disabled=true;}}
form.addEventListener('submit',async event=>{event.preventDefault();if(!state)return;save.disabled=true;message.textContent='Mentés és ellenőrzés folyamatban…';
 try{const response=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json','X-Zolee-Settings':'1'},body:JSON.stringify(values())});
 const result=await response.json();if(!response.ok)throw Error(result.message||'HTTP '+response.status);
 state=result.config||state;
 message.textContent=result.restart?'Sikeres mentés. Az eszköz újraindul…':'Sikeres mentés. A változások már érvényesek.';
 if(!result.restart){fill();save.disabled=false;}
 }catch(e){message.textContent='Mentési hiba: '+e.message;save.disabled=false;}
});
async function loxoneLink(){try{const r=await fetch('/api/settings',{cache:'no-store'});if(r.ok&&(await r.json()).DataReceptMode==='loxone'){const a=document.createElement('a');a.href='/loxone';a.textContent='Loxone beállítások';document.body.prepend(a);}}catch{}}
loxoneLink();load();
