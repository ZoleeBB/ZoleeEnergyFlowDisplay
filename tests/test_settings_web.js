/* A valódi beállítási JS: módváltás, kölcsönös kizárás és mentett JSON. */
const fs=require('fs'),vm=require('vm'),assert=require('node:assert/strict');
const nodes=new Map();
class Element {
 constructor(tag='input'){this.tagName=tag.toUpperCase();this.children=[];this.value='';this.checked=false;this.disabled=false;this.hidden=false;}
 set id(id){this._id=id;nodes.set(id,this);}get id(){return this._id;}
 append(...children){this.children.push(...children);for(const child of children)child.parentElement=this;if(this.tagName==='SELECT'&&this.children.length===1)this.value=children[0].value;}
 prepend(...children){this.children.unshift(...children);}
 addEventListener(name,callback){this['on'+name]=callback;}
}
for(const id of ['settings','fields','message','save']){const element=new Element();element.id=id;}
const configuration=JSON.parse(fs.readFileSync('config.sample.json','utf8'));
configuration.DataSeedEnabled=1;
let posted;
const sandbox={console,structuredClone,document:{body:new Element('body'),getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},fetch:async(url,options={})=>{
 if(options.method==='POST'){
  assert.equal(options.headers['X-Zolee-Settings'],'1');posted=JSON.parse(options.body);
  return {ok:true,json:async()=>({ok:true,restart:true,config:posted})};
 }
 return {ok:true,json:async()=>structuredClone(configuration)};
}};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('assets/web/settings.js','utf8')+'\nglobalThis.api={load,values,sections};',sandbox);
(async()=>{
 await sandbox.api.load();
 const mode=nodes.get('DataReceptMode'),seed=nodes.get('DataSeedEnabled'),sections=sandbox.api.sections;
 assert(mode.children.some(option=>option.value==='slave'));
 assert(seed.checked&&!seed.disabled&&!sections.get('mqtt.broker').hidden);
 mode.value='slave';mode.onchange();
 assert(!seed.checked&&seed.disabled);
 assert(sections.get('mqtt.broker').hidden&&sections.get('http.data_timeout_seconds').hidden);
 assert(sections.get('time.ntp_server').hidden&&!nodes.get('slave-wifi-hint').hidden);
 nodes.get('wifi.ssid').value='';
 nodes.get('ota.auto_update').checked=true;
 await nodes.get('settings').onsubmit({preventDefault(){}});
 assert.equal(posted.DataReceptMode,'slave');assert.equal(posted.DataSeedEnabled,0);
 assert.equal(posted.wifi.ssid,'');assert.equal(posted.mqtt.broker,'');
 assert.equal(posted.ota.auto_update,1);
 assert(nodes.get('message').textContent.includes('újraindul'));
 configuration.DataReceptMode='http';configuration.DataSeedEnabled=0;
 await sandbox.api.load();
 assert(!seed.disabled&&sections.get('mqtt.broker').hidden);
 assert(!sections.get('http.data_timeout_seconds').hidden&&!sections.get('time.ntp_server').hidden);
 assert(nodes.get('slave-wifi-hint').hidden);
 console.log('PASS: actual settings JS switches Slave without Wi-Fi/MQTT/NTP, prevents relay mode and saves canonical config');
})().catch(error=>{console.error(error);process.exitCode=1;});
