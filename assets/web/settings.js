'use strict';
const definitions=[
['Általános',[
['DataReceptMode','Adatfogadási mód','mode'],['EnergyFlowDisabled','Energiaáramlás tiltás','bool'],['project.title','Projekt neve'],['project.language','Nyelv','language'],['project.currency','Pénznem']]],
['Bluetooth adatmegosztás',[
['DataSeedEnabled','Adat seed – master kijelző (BLE)','bool'],['DataSeedPin','Párosítási PIN (6 számjegy, üresen automatikus)','pin']]],
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
['Kijelző', [['display.day_brightness','Nappali fényerő (%)','integer'],['display.night_brightness','Éjszakai fényerő (%)','integer'],['display.rotation_degrees','Kijelző tájolása','rotation'],['display.touch_wake_seconds','Ébrenlét érintés után (mp, Loxone jelenlétvezérlésnél)','integer'],['display.jpeg_backgrounds','Hardveres JPEG háttérdekódolás (kísérleti, ha a sablon tartalmaz JPEG hátteret)','bool']]],
['Statisztika', [['statistics.save_interval_minutes','Mentési időköz (perc, maximum 15)','integer'],['statistics.max_integration_gap_seconds','Maximális mintaköz (másodperc)','integer']]],
['Energia és költség',[
['energy.battery_capacity_kwh','Akkumulátor maximális kapacitása (kWh, 0 = ismeretlen)','number'],['energy.battery_min_soc_pct','Akku alsó SOC-határa a hátralévő időhöz (%, 0 = alapértelmezés: 15%, szigetüzemben 5%)','number'],['energy.electricity_purchase_price','Vételi ár','number'],['energy.feed_in_price','Visszatáplálási ár','number'],
['energy.co2_factor_kg_per_kwh','CO₂ tényező (kg/kWh)','number'],['energy.tree_absorption_kg_per_year','Fa elnyelés (kg/év)','number']]],
['Frissítés', [['ota.auto_update','Automatikus rendszerfrissítés éjfélkor','bool']]],
['Webes hozzáférés', [['ota.username','Adminisztrátori felhasználónév'],['ota.password','Adminisztrátori jelszó','secret']]]];
const form=document.getElementById('settings'),holder=document.getElementById('fields'),message=document.getElementById('message'),save=document.getElementById('save');
let state=null;
const fieldTypes=new Map();
const sections=new Map();
const byPath=p=>document.getElementById(p);
const nestedGet=(obj,path)=>path.split('.').reduce((v,k)=>v?.[k],obj);
function nestedSet(obj,path,value){const a=path.split('.'),last=a.pop();const target=a.reduce((v,k)=>v[k],obj);target[last]=value;}
for(const [title,fields] of definitions){
 const section=document.createElement('section'),h=document.createElement('h2'),grid=document.createElement('div');h.textContent=title;grid.className='grid';
 sections.set(fields[0][0],section);
 for(const [path,labelText,type='text'] of fields){
  const label=document.createElement('label'),input=document.createElement(type==='mode'||type==='language'||type==='rotation'?'select':'input');
  label.textContent=labelText;input.id=path;fieldTypes.set(path,type);
  if(type==='bool')input.type='checkbox';
  else if(type==='pin'){input.type='text';input.inputMode='numeric';input.maxLength=6;input.pattern='[0-9]{6}|';}
  else if(type==='integer'||type==='number'){input.type='number';input.step=type==='integer'?'1':'any';}
  else if(type==='secret'){input.type='password';input.autocomplete='new-password';input.placeholder='Változatlan jelszó';}
  else if(input.tagName==='INPUT')input.type='text';
  if(type==='mode'||type==='language'||type==='rotation')for(const [value,text] of (type==='mode'?[['mqtt','MQTT'],['http','HTTP'],['loxone','Loxone'],['slave','Slave (Bluetooth)']]:type==='language'?[['hu','Magyar'],['en','Angol']]:[['270','270° – csatlakozók jobb oldalon'],['90','90° – csatlakozók bal oldalon']])){
   const option=document.createElement('option');option.value=value;option.textContent=text;input.append(option);
  }
  label.append(input);
  if(type==='secret'){const clear=document.createElement('label'),check=document.createElement('input');clear.className='clear';clear.textContent='Meglévő jelszó törlése';check.type='checkbox';check.id=path+'__clear';clear.prepend(check);label.append(clear);check.onchange=()=>{input.disabled=check.checked;if(check.checked)input.value='';};}
  grid.append(label);
 }
 section.append(h,grid);holder.append(section);
 if(fields[0][0]==='ota.auto_update'){
  const hint=document.createElement('p');hint.className='hint';
  hint.textContent='Napváltáskor ellenőrzi a ZoleeBB/ZoleeEnergyFlowDisplay legújabb stabil GitHub-release-ét. Újabb verzió esetén mentés után automatikusan frissít és újraindul. Internetelérés és Wi-Fi szükséges, Slave módban is. A sablonokat nem írja felül. Alapból kikapcsolva.';
  section.append(hint);
 }
 if(fields[0][0]==='wifi.ssid'){
  const hint=document.createElement('p');hint.id='slave-wifi-hint';hint.className='hint';
  hint.textContent='Slave módban a párosított mastertől automatikusan átveszi a Wi-Fi hálózat nevét, jelszavát és az automatikus frissítés kapcsolóját. Saját IP-címet DHCP-vel kap. Az első csatlakozáskor a kijelzőn kell megadni a master PIN-jét. Ezeket a beállításokat a masteren módosítsd.';
  section.append(hint);
 }
 if(fields.some(([path])=>path==='DataSeedEnabled')){
  const hint=document.createElement('p');hint.className='hint';
  hint.textContent='Az energiaadatok nyilvánosan olvashatók. A Wi-Fi és az automatikus frissítés átvétele csak PIN-nel hitelesített, titkosított párosítás után lehetséges. A master diagnosztikáján látható a PIN, és ott engedélyezhető új párosítás 2 percre. PIN-váltás törli a korábbi párosításokat. A PIN helyi fizikai hozzáféréssel leolvasható.';
  section.append(hint);
  const pairing=document.createElement('button');pairing.id='ble-pairing-open';pairing.type='button';pairing.textContent='Új párosítás engedélyezése 2 percre';
  pairing.onclick=async()=>{try{const response=await fetch('/api/ble/pairing',{method:'POST',headers:{'X-Zolee-Settings':'1'}});const text=await response.text();if(!response.ok)throw Error(text);message.textContent=text;}catch(error){message.textContent=error.message;}};
  section.append(pairing);
 }
}
function updateVisibility(){
 const mode=byPath('DataReceptMode').value,slave=mode==='slave';
 const disabled=byPath('EnergyFlowDisabled');disabled.disabled=mode!=='loxone';if(mode!=='loxone')disabled.checked=false;
 sections.get('mqtt.broker').hidden=mode!=='mqtt';
 sections.get('http.data_timeout_seconds').hidden=mode!=='http';
 sections.get('time.ntp_server').hidden=slave;
 byPath('slave-wifi-hint').hidden=!slave;
 const seed=byPath('DataSeedEnabled');seed.disabled=slave;if(slave)seed.checked=false;
 byPath('DataSeedPin').parentElement.hidden=!seed.checked;
 byPath('ble-pairing-open').hidden=!seed.checked;
 byPath('ota.auto_update').disabled=slave;
 byPath('DataReceptMode').disabled=!!state?.cardless_slave;
}
byPath('DataReceptMode').onchange=updateVisibility;
byPath('DataSeedEnabled').onchange=updateVisibility;
function fill(){for(const [path,type] of fieldTypes){const e=byPath(path),value=nestedGet(state,path);if(type==='bool')e.checked=!!value;else e.value=(value == null ? '' : value);if(type==='secret'){const clear=byPath(path+'__clear');clear.checked=false;e.disabled=false;}}updateVisibility();}
function values(){const result=structuredClone(state);for(const [path,type] of fieldTypes){
 const input=byPath(path);let value;
 if(type==='bool')value=input.checked?1:0;
 else if(type==='integer'||type==='number'||type==='rotation')value=Number(input.value);
 else if(type==='secret')value=byPath(path+'__clear').checked?'':(input.value||null);
 else value=input.value;
 nestedSet(result,path,value);
 }return result;
}
async function load(){message.textContent='Beállítások betöltése…';try{const response=await fetch('/api/settings',{cache:'no-store'});if(!response.ok)throw Error(await response.text());state=await response.json();fill();message.textContent=state.cardless_slave?'SD nélküli Slave: csak Rendszer téma; beállítások a belső tárban.':'';}catch(e){message.textContent='Betöltési hiba: '+e.message;save.disabled=true;}}
form.addEventListener('submit',async event=>{event.preventDefault();if(!state)return;save.disabled=true;message.textContent='Mentés és ellenőrzés folyamatban…';
 try{const response=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json','X-Zolee-Settings':'1'},body:JSON.stringify(values())});
 const result=await response.json();if(!response.ok)throw Error(result.message||'HTTP '+response.status);
 state=result.config||state;
 message.textContent=result.restart?'Sikeres mentés. Az eszköz újraindul…':'Sikeres mentés. A változások már érvényesek.';
 if(!result.restart){fill();save.disabled=false;}
 }catch(e){message.textContent='Mentési hiba: '+e.message;save.disabled=false;}
});
async function loxoneLink(){try{const r=await fetch('/api/settings',{cache:'no-store'});if(r.ok&&(await r.json()).DataReceptMode==='loxone'){const a=document.createElement('a');a.href='/loxone';a.className='button secondary';a.textContent='Loxone beállítások';const p=document.createElement('p');p.append(a);document.querySelector('h1').after(p);}}catch{}}
loxoneLink();load();
