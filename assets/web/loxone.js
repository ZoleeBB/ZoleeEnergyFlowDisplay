'use strict';
const fields=[['PowerGrid','Hálózati teljesítmény (kW)',1],['PowerBat','Akku teljesítmény (kW)',1],['PowerPV','Napelem teljesítmény (kW)',1],['PowerFull','Ház fogyasztása (kW)',1],['SocBat','Töltöttség (%)',1],['Status','Rendszerállapot (1–5)',1],['NightMode','Éjszakai mód (0/1)',1],['BatteryTemperature','Akku hőmérséklete (°C)'],['Temperature','Külső hőmérséklet (°C)'],['RelativeHumidity','Relatív páratartalom (%)'],['WindSpeed','Szélsebesség (km/h)'],['HasSunRadiation','Napsugárzás (0/1)'],['HasPrecipitation','Csapadék (0/1)'],['SunAzimuth','Nap azimutja (°)'],['SunAltitude','Nap magassága (°)'],['Sunrise','Napkelte (perc éjféltől)'],['Sunset','Napnyugta (perc éjféltől)'],['Presence','Jelenlét a Master előtt (0/1)'],['MailArrived','Posta érkezett (0/1)']];
const displayFields=['NightMode','Presence','MailArrived'];
const disabledHidden=key=>energyDisabled&&!displayFields.includes(key);
const sunFields=['SunAzimuth','SunAltitude','Sunrise','Sunset'];
// Ezeknél a jeleknél a szorzó/eltolás értelmetlen: fixen 1 és 0 megy a kijelzőre.
const plainFields=['WindSpeed','HasSunRadiation','HasPrecipitation',...sunFields,'Presence','MailArrived'];
const scaled=key=>!plainFields.includes(key);
const $=id=>document.getElementById(id);let catalog=[],catalogSnapshot='',initialized=false,busy=false,serverBusy=false,requestPending=false,feedback='',energyDisabled=false;
for(const [key,label,required] of fields){const row=document.createElement('tr');row.id='mapping-row-'+key;const title=document.createElement('th');title.scope='row';title.textContent=label+(required?' *':'');row.append(title);for(const type of ['uuid','scale','offset']){const cell=document.createElement('td');if(type!=='uuid'&&!scaled(key)){cell.className='muted';cell.textContent='—';row.append(cell);continue;}const el=document.createElement(type==='uuid'?'select':'input');el.id=key+'_'+type;el.setAttribute('aria-label',label+' – '+(type==='uuid'?'Loxone állapot':type==='scale'?'Szorzó':'Eltolás'));if(type!=='uuid'){el.type='number';el.step='any';el.value=type==='scale'?'1':'0';}cell.append(el);row.append(cell);}$('mappings').append(row);}
function filters(){for(const [id,property,label] of [['room-filter','room','Összes helyiség'],['category-filter','category','Összes funkció']]){const select=$(id),selected=select.value;select.replaceChildren();const all=document.createElement('option');all.value='';all.textContent=label;select.append(all);const names=[...new Set(catalog.map(c=>c[property]||''))].sort((a,b)=>a.localeCompare(b,'hu'));for(const name of names){const o=document.createElement('option');o.value=JSON.stringify(name);o.textContent=name||(property==='room'?'Nincs helyiség':'Nincs funkció');select.append(o);}select.value=names.some(name=>JSON.stringify(name)===selected)?selected:'';}}
function options(){const room=$('room-filter').value,category=$('category-filter').value;for(const [key] of fields){const select=$(key+'_uuid'),selected=select.value;select.replaceChildren();let found=false;const blank=document.createElement('option');blank.value='';blank.textContent='Nincs hozzárendelve';select.append(blank);for(const c of catalog){if(!c.uuid)continue;if(c.uuid!==selected&&((room&&JSON.stringify(c.room||'')!==room)||(category&&JSON.stringify(c.category||'')!==category)))continue;const o=document.createElement('option');o.value=c.uuid;o.textContent=(c.name||'Névtelen')+' ('+(c.room||'–')+' / '+(c.category||'–')+')'+(c.state?' · '+c.state:'');select.append(o);if(c.uuid===selected)found=true;}if(selected&&!found){const o=document.createElement('option');o.value=selected;o.textContent='Korábban párosított / nem található: '+selected;select.append(o);}select.value=selected;}}
function values(){const mappings={};for(const [key] of fields){if(disabledHidden(key))continue;mappings[key]=scaled(key)?{uuid:$(key+'_uuid').value,scale:Number($(key+'_scale').value),offset:Number($(key+'_offset').value)}:{uuid:$(key+'_uuid').value,scale:1,offset:0};}return {host:$('host').value.trim(),port:Number($('port').value),tls:$('tls').checked,ca_pem:$('ca_pem').value.trim(),username:$('username').value,password:$('password').value||null,mappings};}
function message(text,error=false){const changed=$('status').textContent!==text;for(const id of ['status','save-status','connection-status']){const el=$(id);if(el){el.textContent=text;el.className=error?'feedback-error':'';}}if(error&&changed)$('status').scrollIntoView?.({block:'nearest',behavior:'smooth'});}
function buttons(){busy=serverBusy||requestPending||!!presencePending;if($('connection-save'))$('connection-save').disabled=busy;$('test').disabled=busy;$('save').disabled=busy;presenceControls();}
async function apiFetch(url,options={},timeoutMs=15000){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),timeoutMs);try{return await fetch(url,{...options,signal:controller.signal});}finally{clearTimeout(timer);}}
const presenceKeys=['blank_at_day','blank_at_night','timeout_seconds'];
let presenceBase=null,presenceLatest=null,presencePending=null,presenceDirty=false,presenceOnline=false,presenceSending=false,presenceGeneration=0;
const samePresence=(a,b)=>a&&b&&presenceKeys.every(key=>a[key]===b[key]);
const presenceValues=()=>({blank_at_day:$('presence-blank_at_day').checked,blank_at_night:$('presence-blank_at_night').checked,timeout_seconds:$('presence-timeout_seconds').value.trim()?Number($('presence-timeout_seconds').value):NaN});
function presenceTell(text,error=false){$('presence-status').textContent=text;$('presence-status').className=error?'feedback-error':'';}
function presenceControls(){$('presence-inactive').hidden=!energyDisabled;const locked=energyDisabled||!presenceBase||!!presencePending||presenceSending;for(const key of presenceKeys)$('presence-'+key).disabled=locked;$('presence-save').disabled=busy||!presenceOnline||!presenceDirty||locked;$('presence-discard').disabled=locked||!presenceOnline||!presenceDirty;}
function loadPresence(value){presenceBase={...value};presenceDirty=false;for(const key of presenceKeys){const el=$('presence-'+key);if(key==='timeout_seconds')el.value=value[key];else el.checked=value[key];}}
function applyPresence(state){
 const value={blank_at_day:state.presence?.blank_at_day??false,blank_at_night:state.presence?.blank_at_night??true,timeout_seconds:state.presence?.timeout_seconds??60},reconnected=!presenceOnline;presenceLatest=value;presenceOnline=true;
 if(presencePending){
  if(state.busy){presenceTell('Beállítások átvéve, tartós mentés folyamatban…');return;}
  if(samePresence(value,presencePending)){presencePending=null;loadPresence(value);presenceTell('Beállítások tartósan elmentve.');}
  else{presencePending=null;presenceTell('A mentés nem igazolt; az eszköz eltérő beállítást mutat. A szerkesztésed megmaradt, újrapróbálhatod. '+(state.status||''),true);}
 }else if(!presenceDirty){if(!samePresence(value,presenceBase)){loadPresence(value);presenceTell('Az eszközről beolvasott beállítások.');}}
 else if(!samePresence(value,presenceBase))presenceTell('A beállítás közben másik helyen változott. A saját szerkesztésed megmaradt; ellenőrizd, vagy vesd el.',true);
 else if(reconnected)presenceTell('Kapcsolat helyreállt. A módosítások még nincsenek elmentve.');
}
async function savePresence(){
 if($('presence-save').disabled)return;
 const desired=presenceValues();if(!Number.isInteger(desired.timeout_seconds)||desired.timeout_seconds<1||desired.timeout_seconds>86400){presenceTell('A sötétítési késleltetés 1 és 86400 közötti egész másodperc legyen.',true);return;}
 if(!samePresence(presenceLatest,presenceBase)){presenceTell('A beállítás közben változott. Vesd el a szerkesztést, majd ellenőrizd az új értékeket.',true);return;}
 // A korábban indult GET nem igazolhatja vagy írhatja felül az új mentést.
 presenceGeneration++;presencePending=desired;presenceSending=true;requestPending=true;buttons();presenceTell('Mentés folyamatban…');
 try{const r=await apiFetch('/api/loxone/save',{method:'POST',headers:{'Content-Type':'application/json','X-Zolee-Loxone':'1'},body:JSON.stringify({section:'presence',presence:desired})});if(!r.ok){presencePending=null;presenceGeneration++;throw Error(await r.text());}presenceTell('Beállítások átvéve, tartós mentés folyamatban…');}
 catch(e){presenceTell('A mentés nem igazolt. A szerkesztésed megmaradt. '+(e.name==='AbortError'?'Az eszköz nem válaszolt 15 másodpercen belül.':e.message),true);}
 finally{presenceSending=false;requestPending=false;buttons();}
 // Elveszett POST-válasznál is csak visszaolvasunk, nem küldjük újra a kérést.
 if(presencePending)await refresh();
}
function validate(action,scope='mappings'){
 const data=values();
 if(scope==='connection'){
 if(!data.host||!data.username)return 'Add meg a Miniserver címét és a felhasználónevet.';
 if(!/^[a-z0-9.-]+$/i.test(data.host))return 'A Miniserver mezőbe csak IP-címet vagy gépnevet írj, http:// előtag és útvonal nélkül.';
 if(!Number.isInteger(data.port)||data.port<1||data.port>65535)return 'A port 1 és 65535 közötti egész szám legyen.';
 if(data.tls&&!data.ca_pem.includes('-----BEGIN CERTIFICATE-----'))return 'A WSS be van kapcsolva, de nincs megadva CA-tanúsítvány. Add meg a PEM-tanúsítványt, vagy helyi WS-kapcsolathoz kapcsold ki a WSS jelölést és használd a Miniserver HTTP-portját (általában 80).';
 return '';
 }
 for(const [key,label,required] of fields){
  if(disabledHidden(key))continue;
  if(action==='save'&&required&&!data.mappings[key].uuid)return 'Hiányzó kötelező párosítás: '+label+'. Előbb kérd le az adatlistát, majd válaszd ki a megfelelő állapotot.';
  if(scaled(key)&&(!$(key+'_scale').value.trim()||!$(key+'_offset').value.trim()||!Number.isFinite(data.mappings[key].scale)||!Number.isFinite(data.mappings[key].offset)))return 'Érvénytelen szorzó vagy eltolás: '+label+'.';
 }
 if(action==='save'&&!energyDisabled){const count=sunFields.filter(key=>data.mappings[key].uuid).length;if(count&&count!==4)return 'A nappályához mind a négy adatot párosítsd: azimut, magasság, napkelte és napnyugta.';}
 return '';
}
let mediaUploadActive=false,loxoneRefreshing=false,transferGeneration=0;
function setLoxoneTransferBusy(active){mediaUploadActive=active;if(active)transferGeneration++;}
async function refresh(){
 if(mediaUploadActive||loxoneRefreshing)return;loxoneRefreshing=true;const generation=transferGeneration,presenceReadGeneration=presenceGeneration;try{
 const r=await apiFetch('/api/loxone',{cache:'no-store'});if(mediaUploadActive||generation!==transferGeneration)return;if(!r.ok)throw Error(await r.text());const state=await r.json();if(mediaUploadActive||generation!==transferGeneration)return;
 if(!initialized){for(const key of ['host','port','username','ca_pem'])$(key).value=state[key]??'';$('tls').checked=state.tls;for(const [key] of fields){const m=state.mappings[key];if(m){const o=document.createElement('option');o.value=m.uuid;o.textContent=m.uuid;$(key+'_uuid').append(o);$(key+'_uuid').value=m.uuid;if(scaled(key)){$(key+'_scale').value=m.scale;$(key+'_offset').value=m.offset;}}}$('password').placeholder=state.has_password?'Mentett jelszó változatlan':'Add meg a jelszót (másik kijelzőn újra szükséges)';initialized=true;}
 energyDisabled=!!state.energy_flow_disabled;
 for(const [key] of fields)$('mapping-row-'+key).hidden=disabledHidden(key);
 if($('mapping-help')){$('mapping-help').hidden=energyDisabled;}
 const next=JSON.stringify(state.catalog||[]);if(next!==catalogSnapshot){catalogSnapshot=next;catalog=(state.catalog||[]).slice().sort((a,b)=>(a.name||'').localeCompare(b.name||'','hu')||(a.state||'').localeCompare(b.state||'','hu'));filters();options();}
 serverBusy=!!state.busy;if(presenceReadGeneration===presenceGeneration&&!presenceSending)applyPresence(state);buttons();message(feedback||state.status,!!feedback);
 if(typeof LoxoneHome!=='undefined')await LoxoneHome.refresh(catalog,serverBusy,energyDisabled);
 if(typeof LoxoneSlaves!=='undefined')await LoxoneSlaves.refresh(catalog,serverBusy);
 }catch(e){if(!mediaUploadActive&&generation===transferGeneration){if(presenceReadGeneration===presenceGeneration&&!presenceSending){presenceOnline=false;presenceTell('A beállítások most nem olvashatók vissza. A szerkesztésed megmaradt. '+e.message,true);buttons();}if(!feedback)message('Kapcsolati hiba: '+(e.name==='AbortError'?'Az eszköz nem válaszolt 15 másodpercen belül.':e.message),true);}}finally{loxoneRefreshing=false;}
}
async function submit(action,scope=action==='test'?'connection':'mappings'){
 if(busy)return;
 if(!initialized){feedback='Az eszköz beállításait még nem sikerült betölteni. Ellenőrizd az eszközkapcsolatot, majd töltsd újra az oldalt.';message(feedback,true);return;}
 feedback=validate(action,scope);if(feedback){message(feedback,true);return;}
 requestPending=true;buttons();message(action==='save'?'Mentés folyamatban…':'Kapcsolat tesztelése…');
 try{const r=await apiFetch('/api/loxone/'+action,{method:'POST',headers:{'Content-Type':'application/json','X-Zolee-Loxone':'1'},body:JSON.stringify({...values(),section:scope})});if(!r.ok)throw Error(await r.text());await refresh();}
 catch(e){feedback=e.name==='AbortError'?'Az eszköz nem válaszolt 15 másodpercen belül. Ellenőrizd a kapcsolatot, majd próbáld újra.':e.message;message(feedback,true);}
 finally{requestPending=false;buttons();}
}
$('tls').addEventListener('change',()=>{if($('tls').checked&&$('port').value==='80')$('port').value='443';else if(!$('tls').checked&&$('port').value==='443')$('port').value='80';});
for(const key of presenceKeys)$('presence-'+key).oninput=$('presence-'+key).onchange=()=>{presenceDirty=!samePresence(presenceBase,presenceValues());presenceTell(presenceDirty?'Még nincs elmentve.':'Nincs módosítás.');buttons();};
$('presence-save').onclick=savePresence;$('presence-discard').onclick=()=>{if($('presence-discard').disabled)return;loadPresence(presenceLatest);presenceTell('Az eszközről beolvasott beállítások.');buttons();};buttons();
$('room-filter').addEventListener('change',options);$('category-filter').addEventListener('change',options);if($('connection-save'))$('connection-save').onclick=()=>submit('save','connection');$('test').onclick=()=>submit('test');$('save').onclick=()=>submit('save');refresh();setInterval(refresh,2000);
