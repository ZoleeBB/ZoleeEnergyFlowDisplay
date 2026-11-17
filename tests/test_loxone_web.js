/* A tényleges webes JS, minimális DOM-mal és szimulált eszköz-API-val. */
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const nodes=new Map();
class Element {
 constructor(tag='input'){this.tagName=tag.toUpperCase();this.children=[];this._value='';this.disabled=false;this.checked=false;this.textContent='';}
 set id(id){this._id=id;nodes.set(id,this);}get id(){return this._id;}
 set value(v){this._value=String(v);}get value(){return this._value;}
 append(child){this.children.push(child);if(this.tagName==='SELECT'&&this.children.length===1)this.value=child.value;}
 replaceChildren(){this.children=[];this.value='';}
 addEventListener(name,callback){this['on'+name]=callback;}
 get options(){return this.children;}
}
for(const id of ['host','port','username','password','ca_pem','room-filter','category-filter','tls','test','save','status','save-status','connection-save','connection-status','mappings','presence-blank_at_day','presence-blank_at_night','presence-timeout_seconds','presence-save','presence-discard','presence-status','presence-inactive']){const e=new Element();e.id=id;}
const keys=['PowerGrid','PowerBat','PowerPV','PowerFull','SocBat','Status','NightMode','BatteryTemperature','Temperature','WindSpeed','HasSunRadiation','HasPrecipitation','SunAzimuth','SunAltitude','Sunrise','Sunset','Presence','MailArrived','RelativeHumidity'];
const id='11223344-5566-7788-99aabbccddeeff00';const sent=[];let rejectPost=false,blockGet=false,rejectGet=null,resolveGet=null,getCalls=0,presenceMode='persist',offlineGet=false,pendingPresence=null;
const state={host:'',port:80,tls:false,ca_pem:'',username:'',has_password:false,busy:false,status:'Paraméterezés szükséges.',catalog:[],mappings:Object.fromEntries(keys.map(k=>[k,{uuid:'',scale:1,offset:0}]))};
const sandbox={console,structuredClone,AbortController,setTimeout,clearTimeout,setInterval:()=>0,document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},fetch:async(url,options={})=>{
 if(options.method!=='POST'){getCalls++;if(offlineGet)throw Error('Nincs kapcsolat.');if(blockGet)return await new Promise((resolve,reject)=>{rejectGet=reject;resolveGet=resolve;});}
 if(options.method==='POST'){
  if(rejectPost)return {ok:false,text:async()=>'A felhasználó vagy jelszó érvénytelen.'};
  const body=JSON.parse(options.body);sent.push({url,body,headers:options.headers});
  if(body.section==='presence'){
   assert.deepEqual(Object.keys(body).sort(),['presence','section']);
   if(presenceMode==='reject')return {ok:false,status:409,text:async()=>'Az eszköz foglalt.'};
   if(presenceMode==='drop')throw Error('A kérés nem jutott el az eszközig.');
   pendingPresence=structuredClone(body.presence);
   if(presenceMode==='pending'){state.busy=true;state.status='Mentés folyamatban…';}
   else if(presenceMode==='failure'){state.busy=false;state.status='Az SD-mentés sikertelen. A korábbi beállítások maradtak érvényben.';}
   else{state.presence=pendingPresence;state.busy=false;state.status='Beállítások elmentve.';}
   if(presenceMode==='lost')throw Error('Elveszett válasz.');
   if(presenceMode==='offline')offlineGet=true;
   return {ok:true,status:202};
  }
  state.catalog=[{uuid:id,name:'<img src=x onerror=alert(1)> PV',room:'Gépészet',category:'Energia',state:'value',type:'InfoOnlyAnalog'}];
  state.status=url.endsWith('save')?'Beállítások elmentve.':'Sikeres kapcsolat.';
  return {ok:true,json:async()=>({accepted:true})};
 }
 return {ok:true,json:async()=>structuredClone(state)};
}};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('assets/web/loxone.js','utf8')+'\nglobalThis.api={refresh,values,options,submit,setLoxoneTransferBusy};',sandbox);
(async()=>{
 await new Promise(resolve=>setImmediate(resolve));
 await sandbox.api.refresh();assert.equal(nodes.get('port').value,'80');assert.equal(nodes.get('Status_uuid').options.length,1);
 assert.equal(nodes.get('presence-blank_at_day').checked,false);assert.equal(nodes.get('presence-blank_at_night').checked,true);
 assert.equal(nodes.get('presence-timeout_seconds').value,'60');assert(nodes.get('presence-save').disabled);
 nodes.get('host').value='192.168.2.10';nodes.get('username').value='display';nodes.get('password').value='test-password';
 nodes.get('tls').checked=true;nodes.get('tls').onchange();assert.equal(nodes.get('port').value,'443');
 await sandbox.api.submit('test');assert.equal(sent.length,0);assert(nodes.get('status').textContent.includes('CA-tanúsítvány'));
 await sandbox.api.refresh();assert(nodes.get('status').textContent.includes('CA-tanúsítvány'));assert(!nodes.get('test').disabled);
 assert.equal(nodes.get('save-status').textContent,nodes.get('status').textContent);
 nodes.get('tls').checked=false;nodes.get('tls').onchange();assert.equal(nodes.get('port').value,'80');
 await sandbox.api.submit('test');assert.equal(sent[0].url,'/api/loxone/test');assert.equal(sent[0].headers['X-Zolee-Loxone'],'1');
 assert.equal(nodes.get('password').value,'test-password');assert.equal(nodes.get('PowerPV_uuid').options.length,2);
 assert(nodes.get('PowerPV_uuid').options[1].textContent.includes('<img'));
 for(const key of keys.slice(0,8))nodes.get(key+'_uuid').value=id;
 nodes.get('PowerGrid_scale').value='-0.001';nodes.get('room-filter').value=JSON.stringify('Másik helyiség');sandbox.api.options();
 assert.equal(nodes.get('PowerPV_uuid').value,id);assert.equal(nodes.get('PowerPV_uuid').options.length,2);
 await sandbox.api.submit('save');assert.equal(sent[1].url,'/api/loxone/save');assert.equal(sent[1].body.mappings.PowerGrid.scale,-0.001);
 assert.equal(sent[1].body.mappings.Temperature.uuid,'');
 assert.equal(sent[1].body.mappings.RelativeHumidity.uuid,'');
 const rows=nodes.get('mappings').children.map(row=>row.id);
 assert.equal(rows.indexOf('mapping-row-RelativeHumidity'),rows.indexOf('mapping-row-Temperature')+1);
 nodes.get('RelativeHumidity_uuid').value=id;
 nodes.get('RelativeHumidity_scale').value='100';nodes.get('RelativeHumidity_offset').value='-2.5';
 assert.equal(sandbox.api.values().mappings.RelativeHumidity.scale,100);
 assert.equal(sandbox.api.values().mappings.RelativeHumidity.offset,-2.5);
 const room=nodes.get('room-filter'),category=nodes.get('category-filter');
 const second='aabbccdd-5566-7788-99aabbccddeeff00',third='eeeeeeee-5566-7788-99aabbccddeeff00';
 state.catalog.push({uuid:second,name:'Akku',room:'Nappali',category:'Energia',state:'value'}, {uuid:third,name:'Hőmérő',room:'Nappali',category:'Klíma',state:'value'});
 await sandbox.api.refresh();
 assert(room.options.some(o=>o.textContent==='Nappali'));assert(category.options.some(o=>o.textContent==='Klíma'));
 room.value=JSON.stringify('Nappali');category.value=JSON.stringify('Energia');room.onchange();
 assert.deepEqual(nodes.get('Temperature_uuid').options.map(o=>o.value),['',second]);
 assert.equal(nodes.get('Temperature_uuid').options[1].textContent,'Akku (Nappali / Energia) · value');
 assert.equal(nodes.get('PowerPV_uuid').value,id);assert(nodes.get('PowerPV_uuid').options.some(o=>o.value===id));
 await sandbox.api.refresh();assert.equal(room.value,JSON.stringify('Nappali'));assert.equal(category.value,JSON.stringify('Energia'));
 category.value=JSON.stringify('Klíma');category.onchange();assert.deepEqual(nodes.get('Temperature_uuid').options.map(o=>o.value),['',third]);
 assert.equal(nodes.get('mappings').children[0].children[0].textContent,'Hálózati teljesítmény (kW) *');
 assert(nodes.get('mappings').children[7].children[0].textContent.includes('(°C)'));
 assert(!nodes.has('SunPosition_uuid'));
 // Az időjárás, a nappálya, a jelenlét és a posta soraiban nincs szorzó/eltolás mező.
 for(const key of ['WindSpeed','HasSunRadiation','HasPrecipitation','SunAzimuth','SunAltitude','Sunrise','Sunset','Presence','MailArrived']){
  assert(!nodes.has(key+'_scale'),key);assert(!nodes.has(key+'_offset'),key);
  const row=nodes.get('mapping-row-'+key);assert.equal(row.children[2].textContent,'—');assert.equal(row.children[3].textContent,'—');
 }
 assert(!nodes.get('PowerGrid_scale').disabled);
 nodes.get('SunAzimuth_uuid').value=id;await sandbox.api.submit('save');assert.equal(sent.length,2);assert(nodes.get('status').textContent.includes('mind a négy'));
 for(const key of keys.slice(12,16))nodes.get(key+'_uuid').value=second;
 await sandbox.api.submit('save');assert.equal(sent.length,3);
 assert.equal(sent[2].body.mappings.Sunrise.scale,1);assert.equal(sent[2].body.mappings.Sunrise.offset,0);
 assert.equal(sent[2].body.mappings.Presence.scale,1);assert.equal(sent[2].body.mappings.MailArrived.offset,0);
 assert(!('SunPosition' in sent[2].body.mappings));
 rejectPost=true;await sandbox.api.submit('test');assert(nodes.get('status').textContent.includes('jelszó érvénytelen'));
 await sandbox.api.refresh();assert(nodes.get('status').textContent.includes('jelszó érvénytelen'));assert(!nodes.get('test').disabled);rejectPost=false;
 nodes.get('password').value='';assert.equal(sandbox.api.values().password,null);
 state.busy=true;await sandbox.api.refresh();assert(nodes.get('save').disabled);await sandbox.api.submit('save');assert.equal(sent.length,3);
 const html=fs.readFileSync('assets/web/loxone.html','utf8');assert.equal((html.match(/id="status"/g)||[]).length,1);assert(html.indexOf('id="status"')<html.indexOf('id="mappings"'));
 state.energy_flow_disabled=true;await sandbox.api.refresh();
 assert.equal(nodes.get('mapping-row-PowerGrid').hidden,true);
 assert.equal(nodes.get('presence-inactive').hidden,false);assert(nodes.get('presence-timeout_seconds').disabled&&nodes.get('presence-save').disabled);
 assert.equal(nodes.get('mapping-row-NightMode').hidden,false);
 const disabledValues=sandbox.api.values();assert.deepEqual(Object.keys(disabledValues.mappings),['NightMode','Presence','MailArrived']);
 assert.equal(nodes.get('mapping-row-Presence').hidden,false);assert.equal(nodes.get('mapping-row-MailArrived').hidden,false);
 state.energy_flow_disabled=false;await sandbox.api.refresh();
 assert.equal(nodes.get('mapping-row-PowerGrid').hidden,false);
 assert.equal(nodes.get('presence-inactive').hidden,true);assert(!nodes.get('presence-timeout_seconds').disabled);
 const previousStatus=nodes.get('status').textContent;
 const beforeCalls=getCalls;
 sandbox.api.setLoxoneTransferBusy(true);await sandbox.api.refresh();assert.equal(getCalls,beforeCalls);
 sandbox.api.setLoxoneTransferBusy(false);
 blockGet=true;const pendingRefresh=sandbox.api.refresh();await sandbox.api.refresh();assert.equal(getCalls,beforeCalls+1);
 sandbox.api.setLoxoneTransferBusy(true);sandbox.api.setLoxoneTransferBusy(false);
 rejectGet(new Error('régi lekérés időtúllépése'));await pendingRefresh;
 assert.equal(nodes.get('status').textContent,previousStatus);
 blockGet=false;await sandbox.api.refresh();
 /* A jelenlét külön mentési szakasz: a háttérlekérés és más mentések megőrzik a draftot. */
 const presence=key=>nodes.get('presence-'+key),presenceText=()=>presence('status').textContent;
 const editPresence=(key,value)=>{const input=presence(key);if(key==='timeout_seconds')input.value=value;else input.checked=value;input.oninput();};
 const savePresence=()=>presence('save').onclick();
 state.busy=false;await sandbox.api.refresh();
 editPresence('blank_at_day',true);editPresence('timeout_seconds',45);
 state.energy_flow_disabled=true;await sandbox.api.refresh();
 assert.equal(presence('timeout_seconds').value,'45');assert(presence('blank_at_day').checked);assert(presence('save').disabled);
 state.energy_flow_disabled=false;await sandbox.api.refresh();assert(!presence('save').disabled);
 await sandbox.api.refresh();assert.equal(presence('timeout_seconds').value,'45');assert(presence('blank_at_day').checked);
 await sandbox.api.submit('save','connection');await sandbox.api.submit('save');
 assert(sent.every(item=>!Object.hasOwn(item.body,'presence')));
 assert.equal(presence('timeout_seconds').value,'45');
 presenceMode='pending';await savePresence();
 assert.equal(sent.at(-1).url,'/api/loxone/save');assert.equal(sent.at(-1).headers['X-Zolee-Loxone'],'1');
 assert.deepEqual(sent.at(-1).body,{section:'presence',presence:{blank_at_day:true,blank_at_night:true,timeout_seconds:45}});
 assert(presenceText().includes('folyamatban'));assert(presence('save').disabled&&presence('timeout_seconds').disabled);
 assert(nodes.get('save').disabled&&nodes.get('connection-save').disabled);
 await sandbox.api.refresh();assert(presenceText().includes('folyamatban'));assert.equal(presence('timeout_seconds').value,'45');
 state.presence=pendingPresence;await sandbox.api.refresh();assert(presenceText().includes('folyamatban'));
 state.busy=false;await sandbox.api.refresh();assert(presenceText().includes('tartósan elmentve'));
 assert(!presence('timeout_seconds').disabled&&presence('save').disabled);
 const beforeInvalid=sent.length;
 for(const value of ['',0,-1,1.5,86401]){editPresence('timeout_seconds',value);await savePresence();assert.equal(sent.length,beforeInvalid);assert(presenceText().includes('egész'));}
 presenceMode='persist';editPresence('timeout_seconds',1);editPresence('blank_at_night',false);await savePresence();
 assert.equal(state.presence.timeout_seconds,1);assert.equal(state.presence.blank_at_night,false);
 editPresence('timeout_seconds',86400);editPresence('blank_at_day',false);await savePresence();assert.equal(state.presence.timeout_seconds,86400);
 assert.equal(state.presence.blank_at_day,false);assert.equal(state.presence.blank_at_night,false);
 /* A sikertelen SD-mentés nem jelenthet sikert, a megőrzött draft újraküldhető. */
 presenceMode='failure';editPresence('timeout_seconds',31);await savePresence();
 assert(presenceText().includes('nem igazolt')&&presenceText().includes('SD-mentés sikertelen'));
 assert.equal(presence('timeout_seconds').value,'31');assert(!presence('save').disabled);
 await sandbox.api.refresh();assert(presenceText().includes('nem igazolt'));
 presenceMode='persist';await savePresence();assert.equal(state.presence.timeout_seconds,31);assert(presenceText().includes('tartósan elmentve'));
 presenceMode='reject';editPresence('timeout_seconds',32);await savePresence();
 assert(presenceText().includes('foglalt'));await sandbox.api.refresh();assert(presenceText().includes('foglalt'));assert(!presence('save').disabled);
 presenceMode='lost';await savePresence();assert.equal(state.presence.timeout_seconds,32);assert(presenceText().includes('tartósan elmentve'));
 presenceMode='drop';editPresence('timeout_seconds',33);await savePresence();assert(presenceText().includes('nem igazolt'));assert(!presence('save').disabled);
 presenceMode='offline';await savePresence();assert.equal(presence('timeout_seconds').value,'33');assert(presence('save').disabled);
 offlineGet=false;await sandbox.api.refresh();assert(presenceText().includes('tartósan elmentve'));
 /* Mentetlen szerkesztés közbeni hálózati hiba után egyértelmű a helyreállás. */
 editPresence('timeout_seconds',34);offlineGet=true;await sandbox.api.refresh();
 assert.equal(presence('timeout_seconds').value,'34');assert(presence('save').disabled);
 offlineGet=false;await sandbox.api.refresh();assert(presenceText().includes('Kapcsolat helyreállt'));assert(!presence('save').disabled);
 presence('discard').onclick();
 /* Másik böngésző módosítása mellett a saját érték és a választás megmarad. */
 editPresence('timeout_seconds',34);state.presence.timeout_seconds=35;await sandbox.api.refresh();
 assert.equal(presence('timeout_seconds').value,'34');assert(presenceText().includes('másik helyen'));
 const beforeConflict=sent.length;await savePresence();assert.equal(sent.length,beforeConflict);
 presence('discard').onclick();assert.equal(presence('timeout_seconds').value,'35');assert(presence('save').disabled);
 /* A mentés előtti lassú GET válasza nem minősítheti sikertelennek az új mentést. */
 presenceMode='persist';const stale=structuredClone(state);blockGet=true;
 const staleRefresh=sandbox.api.refresh();editPresence('timeout_seconds',36);await savePresence();
 assert(presence('timeout_seconds').disabled);assert.equal(presence('timeout_seconds').value,'36');
 resolveGet({ok:true,json:async()=>stale});await staleRefresh;
 assert(presence('timeout_seconds').disabled);assert(presenceText().includes('folyamatban'));
 blockGet=false;await sandbox.api.refresh();assert(presenceText().includes('tartósan elmentve'));
 assert.equal(presence('timeout_seconds').value,'36');assert(!presence('timeout_seconds').disabled);
 assert(html.indexOf('id="master-presence"')>html.indexOf('id="mappings"'));
 assert(html.includes('legalább 15 másodpercig'));
 const settings=fs.readFileSync('assets/web/settings.js','utf8');assert(!settings.includes("['display.touch_wake_seconds'"));assert(settings.includes('/loxone#master-presence'));
 console.log('PASS: actual Loxone editor, WSS and mapping regressions; independent presence payload, day/night defaults, draft retention, delay bounds, accepted/persisted/error/retry, offline/lost responses and stale GET protection');
})().catch(e=>{console.error(e);process.exitCode=1;});
