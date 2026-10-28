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
for(const id of ['host','port','username','password','ca_pem','room-filter','category-filter','tls','test','save','status','save-status','mappings']){const e=new Element();e.id=id;}
const keys=['PowerGrid','PowerBat','PowerPV','PowerFull','SocBat','Status','NightMode','BatteryTemperature','Temperature','WindSpeed','HasSunRadiation','HasPrecipitation','SunAzimuth','SunAltitude','Sunrise','Sunset','Presence','MailArrived'];
const id='11223344-5566-7788-99aabbccddeeff00';const sent=[];let rejectPost=false,blockGet=false,rejectGet=null,getCalls=0;
const state={host:'',port:80,tls:false,ca_pem:'',username:'',has_password:false,busy:false,status:'Paraméterezés szükséges.',catalog:[],mappings:Object.fromEntries(keys.map(k=>[k,{uuid:'',scale:1,offset:0}]))};
const sandbox={console,structuredClone,AbortController,setTimeout,clearTimeout,setInterval:()=>0,document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},fetch:async(url,options={})=>{
 if(options.method!=='POST'){getCalls++;if(blockGet)return await new Promise((resolve,reject)=>{rejectGet=reject;});}
 if(options.method==='POST'){
  if(rejectPost)return {ok:false,text:async()=>'A felhasználó vagy jelszó érvénytelen.'};
  sent.push({url,body:JSON.parse(options.body),headers:options.headers});
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
 assert.equal(nodes.get('mapping-row-NightMode').hidden,false);
 const disabledValues=sandbox.api.values();assert.deepEqual(Object.keys(disabledValues.mappings),['NightMode','Presence','MailArrived']);
 assert.equal(nodes.get('mapping-row-Presence').hidden,false);assert.equal(nodes.get('mapping-row-MailArrived').hidden,false);
 state.energy_flow_disabled=false;await sandbox.api.refresh();
 assert.equal(nodes.get('mapping-row-PowerGrid').hidden,false);
 const previousStatus=nodes.get('status').textContent;
 const beforeCalls=getCalls;
 sandbox.api.setLoxoneTransferBusy(true);await sandbox.api.refresh();assert.equal(getCalls,beforeCalls);
 sandbox.api.setLoxoneTransferBusy(false);
 blockGet=true;const pendingRefresh=sandbox.api.refresh();await sandbox.api.refresh();assert.equal(getCalls,beforeCalls+1);
 sandbox.api.setLoxoneTransferBusy(true);sandbox.api.setLoxoneTransferBusy(false);
 rejectGet(new Error('régi lekérés időtúllépése'));await pendingRefresh;
 assert.equal(nodes.get('status').textContent,previousStatus);
 blockGet=false;await sandbox.api.refresh();
 console.log('PASS: visible persistent WSS/server errors, no invalid POST, and actual Loxone web JS, discovery, retained test password, mapping/filter, transforms, optional fields, save and busy state');
})().catch(e=>{console.error(e);process.exitCode=1;});
