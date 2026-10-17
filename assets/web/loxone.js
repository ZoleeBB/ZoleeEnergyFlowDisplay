'use strict';
const fields=[['PowerGrid','Hálózati teljesítmény (kW)',1],['PowerBat','Akku teljesítmény (kW)',1],['PowerPV','Napelem teljesítmény (kW)',1],['PowerFull','Ház fogyasztása (kW)',1],['SocBat','Töltöttség (%)',1],['Status','Rendszerállapot (1–4)',1],['NightMode','Éjszakai mód (0/1)',1],['BatteryTemperature','Akku hőmérséklete (°C)',1],['Temperature','Külső hőmérséklet (°C)'],['WindSpeed','Szélsebesség (km/h)'],['HasSunRadiation','Napsugárzás (0/1)'],['HasPrecipitation','Csapadék (0/1)'],['SunAzimuth','Nap azimutja (°)'],['SunAltitude','Nap magassága (°)'],['Sunrise','Napkelte (perc éjféltől)'],['Sunset','Napnyugta (perc éjféltől)']];
const sunFields=['SunAzimuth','SunAltitude','Sunrise','Sunset'];
const $=id=>document.getElementById(id);let catalog=[],catalogSnapshot='',initialized=false,busy=false,serverBusy=false,requestPending=false,feedback='';
for(const [key,label,required] of fields){const row=document.createElement('tr');const title=document.createElement('td');title.textContent=label+(required?' *':'');row.append(title);for(const type of ['uuid','scale','offset']){const cell=document.createElement('td'),el=document.createElement(type==='uuid'?'select':'input');el.id=key+'_'+type;if(type!=='uuid'){el.type='number';el.step='any';el.value=type==='scale'?'1':'0';}cell.append(el);row.append(cell);}$('mappings').append(row);}
function filters(){for(const [id,property,label] of [['room-filter','room','Összes helyiség'],['category-filter','category','Összes funkció']]){const select=$(id),selected=select.value;select.replaceChildren();const all=document.createElement('option');all.value='';all.textContent=label;select.append(all);const names=[...new Set(catalog.map(c=>c[property]||''))].sort((a,b)=>a.localeCompare(b,'hu'));for(const name of names){const o=document.createElement('option');o.value=JSON.stringify(name);o.textContent=name||(property==='room'?'Nincs helyiség':'Nincs funkció');select.append(o);}select.value=names.some(name=>JSON.stringify(name)===selected)?selected:'';}}
function options(){const room=$('room-filter').value,category=$('category-filter').value;for(const [key] of fields){const select=$(key+'_uuid'),selected=select.value;select.replaceChildren();let found=false;const blank=document.createElement('option');blank.value='';blank.textContent='Nincs hozzárendelve';select.append(blank);for(const c of catalog){if(c.uuid!==selected&&((room&&JSON.stringify(c.room||'')!==room)||(category&&JSON.stringify(c.category||'')!==category)))continue;const o=document.createElement('option');o.value=c.uuid;o.textContent=(c.name||'Névtelen')+' ('+(c.room||'–')+' / '+(c.category||'–')+')'+(c.state?' · '+c.state:'');select.append(o);if(c.uuid===selected)found=true;}if(selected&&!found){const o=document.createElement('option');o.value=selected;o.textContent='Korábban párosított / nem található: '+selected;select.append(o);}select.value=selected;}}
function values(){const mappings={};for(const [key] of fields)mappings[key]={uuid:$(key+'_uuid').value,scale:Number($(key+'_scale').value),offset:Number($(key+'_offset').value)};return {host:$('host').value.trim(),port:Number($('port').value),tls:$('tls').checked,ca_pem:$('ca_pem').value.trim(),username:$('username').value,password:$('password').value||null,mappings};}
function message(text,error=false){const changed=$('status').textContent!==text;for(const id of ['status','save-status']){const el=$(id);if(el){el.textContent=text;el.className=error?'feedback-error':'';}}if(error&&changed)$('status').scrollIntoView?.({block:'nearest',behavior:'smooth'});}
function buttons(){busy=serverBusy||requestPending;$('test').disabled=busy;$('save').disabled=busy;}
async function apiFetch(url,options={}){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),15000);try{return await fetch(url,{...options,signal:controller.signal});}finally{clearTimeout(timer);}}
function validate(action){
 const data=values();
 if(!data.host||!data.username)return 'Add meg a Miniserver címét és a felhasználónevet.';
 if(!/^[a-z0-9.-]+$/i.test(data.host))return 'A Miniserver mezőbe csak IP-címet vagy gépnevet írj, http:// előtag és útvonal nélkül.';
 if(!Number.isInteger(data.port)||data.port<1||data.port>65535)return 'A port 1 és 65535 közötti egész szám legyen.';
 if(data.tls&&!data.ca_pem.includes('-----BEGIN CERTIFICATE-----'))return 'A WSS be van kapcsolva, de nincs megadva CA-tanúsítvány. Add meg a PEM-tanúsítványt, vagy helyi WS-kapcsolathoz kapcsold ki a WSS jelölést és használd a Miniserver HTTP-portját (általában 80).';
 for(const [key,label,required] of fields){
  if(action==='save'&&required&&!data.mappings[key].uuid)return 'Hiányzó kötelező párosítás: '+label+'. Előbb kérd le az adatlistát, majd válaszd ki a megfelelő állapotot.';
  if(!$(key+'_scale').value.trim()||!$(key+'_offset').value.trim()||!Number.isFinite(data.mappings[key].scale)||!Number.isFinite(data.mappings[key].offset))return 'Érvénytelen szorzó vagy eltolás: '+label+'.';
 }
 if(action==='save'){const count=sunFields.filter(key=>data.mappings[key].uuid).length;if(count&&count!==4)return 'A nappályához mind a négy adatot párosítsd: azimut, magasság, napkelte és napnyugta.';}
 return '';
}
async function refresh(){try{
 const r=await apiFetch('/api/loxone',{cache:'no-store'});if(!r.ok)throw Error(await r.text());const state=await r.json();
 if(!initialized){for(const key of ['host','port','username','ca_pem'])$(key).value=state[key]??'';$('tls').checked=state.tls;for(const [key] of fields){const m=state.mappings[key];if(m){const o=document.createElement('option');o.value=m.uuid;o.textContent=m.uuid;$(key+'_uuid').append(o);$(key+'_uuid').value=m.uuid;$(key+'_scale').value=m.scale;$(key+'_offset').value=m.offset;}}$('password').placeholder=state.has_password?'Mentett jelszó változatlan':'Add meg a jelszót (másik kijelzőn újra szükséges)';initialized=true;}
 const next=JSON.stringify(state.catalog||[]);if(next!==catalogSnapshot){catalogSnapshot=next;catalog=(state.catalog||[]).slice().sort((a,b)=>(a.name||'').localeCompare(b.name||'','hu')||(a.state||'').localeCompare(b.state||'','hu'));filters();options();}
 serverBusy=!!state.busy;buttons();message(feedback||state.status,!!feedback);
 }catch(e){if(!feedback)message('Kapcsolati hiba: '+(e.name==='AbortError'?'Az eszköz nem válaszolt 15 másodpercen belül.':e.message),true);}
}
async function submit(action){
 if(busy)return;
 if(!initialized){feedback='Az eszköz beállításait még nem sikerült betölteni. Ellenőrizd az eszközkapcsolatot, majd töltsd újra az oldalt.';message(feedback,true);return;}
 feedback=validate(action);if(feedback){message(feedback,true);return;}
 requestPending=true;buttons();message(action==='save'?'Mentés folyamatban…':'Kapcsolat tesztelése…');
 try{const r=await apiFetch('/api/loxone/'+action,{method:'POST',headers:{'Content-Type':'application/json','X-Zolee-Loxone':'1'},body:JSON.stringify(values())});if(!r.ok)throw Error(await r.text());await refresh();}
 catch(e){feedback=e.name==='AbortError'?'Az eszköz nem válaszolt 15 másodpercen belül. Ellenőrizd a kapcsolatot, majd próbáld újra.':e.message;message(feedback,true);}
 finally{requestPending=false;buttons();}
}
$('tls').addEventListener('change',()=>{if($('tls').checked&&$('port').value==='80')$('port').value='443';else if(!$('tls').checked&&$('port').value==='443')$('port').value='80';});
$('room-filter').addEventListener('change',options);$('category-filter').addEventListener('change',options);$('test').onclick=()=>submit('test');$('save').onclick=()=>submit('save');refresh();setInterval(refresh,2000);
