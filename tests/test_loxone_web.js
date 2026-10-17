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
for(const id of ['host','port','username','password','ca_pem','filter','tls','test','save','status','mappings']){const e=new Element();e.id=id;}
const keys=['PowerGrid','PowerBat','PowerPV','PowerFull','SocBat','Status','NightMode','BatteryTemperature','Temperature','WindSpeed','HasSunRadiation','HasPrecipitation','SunPosition'];
const id='11223344-5566-7788-99aabbccddeeff00';const sent=[];
const state={host:'',port:80,tls:false,ca_pem:'',username:'',has_password:false,busy:false,status:'Paraméterezés szükséges.',catalog:[],mappings:Object.fromEntries(keys.map(k=>[k,{uuid:'',scale:1,offset:0}]))};
const sandbox={console,structuredClone,setInterval:()=>0,document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},fetch:async(url,options={})=>{
 if(options.method==='POST'){
  sent.push({url,body:JSON.parse(options.body),headers:options.headers});
  state.catalog=[{uuid:id,name:'<img src=x onerror=alert(1)> PV',room:'Gépészet',category:'Energia',state:'value',type:'InfoOnlyAnalog'}];
  state.status=url.endsWith('save')?'Beállítások elmentve.':'Sikeres kapcsolat.';
  return {ok:true,json:async()=>({accepted:true})};
 }
 return {ok:true,json:async()=>structuredClone(state)};
}};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('assets/web/loxone.js','utf8')+'\nglobalThis.api={refresh,values,options,submit};',sandbox);
(async()=>{
 await sandbox.api.refresh();assert.equal(nodes.get('port').value,'80');assert.equal(nodes.get('Status_uuid').options.length,1);
 nodes.get('host').value='192.168.2.10';nodes.get('username').value='display';nodes.get('password').value='test-password';
 await sandbox.api.submit('test');assert.equal(sent[0].url,'/api/loxone/test');assert.equal(sent[0].headers['X-Zolee-Loxone'],'1');
 assert.equal(nodes.get('password').value,'test-password');assert.equal(nodes.get('PowerPV_uuid').options.length,2);
 assert(nodes.get('PowerPV_uuid').options[1].textContent.includes('<img'));
 for(const key of keys.slice(0,8))nodes.get(key+'_uuid').value=id;
 nodes.get('PowerGrid_scale').value='-0.001';nodes.get('filter').value='nincs ilyen';sandbox.api.options();
 assert.equal(nodes.get('PowerPV_uuid').value,id);assert.equal(nodes.get('PowerPV_uuid').options.length,2);
 await sandbox.api.submit('save');assert.equal(sent[1].url,'/api/loxone/save');assert.equal(sent[1].body.mappings.PowerGrid.scale,-0.001);
 assert.equal(sent[1].body.mappings.Temperature.uuid,'');
 nodes.get('password').value='';assert.equal(sandbox.api.values().password,null);
 state.busy=true;await sandbox.api.refresh();assert(nodes.get('save').disabled);await sandbox.api.submit('save');assert.equal(sent.length,2);
 console.log('PASS: actual Loxone web JS, discovery, retained test password, mapping/filter, transforms, optional fields, save and busy state');
})().catch(e=>{console.error(e);process.exitCode=1;});
