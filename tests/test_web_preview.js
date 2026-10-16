/* Runs the exact browser ZIP reader with real stored/deflate archives. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const sandbox={DecompressionStream,Blob,TextDecoder,TextEncoder,Uint8Array,DataView,Map,console};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('assets/web/template-manager.js','utf8')+'\nglobalThis.api={unpackZip,validateManifest,pngInfo,drawStoredPreview,previewName};',sandbox);
async function read(path){const b=fs.readFileSync(path);return sandbox.api.unpackZip(b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength));}
(async()=>{
 for(const path of process.argv.slice(2,4)){
  const files=await read(path),doc=JSON.parse(new TextDecoder().decode(files.get('template.json'))),metrics=sandbox.api.validateManifest(doc,files);
  assert.strictEqual(doc.id,'test-layout');assert(metrics.decoded===16);assert(files.size===2);
 }
 // A valódi Nap pálya ZIP-et ugyanazzal a webes validátorral ellenőrizzük.
 const solarFiles=await read(process.argv[4]);
 const solarDoc=JSON.parse(new TextDecoder().decode(solarFiles.get('template.json')));
 const solarMetrics=sandbox.api.validateManifest(solarDoc,solarFiles);
 assert.strictEqual(solarDoc.id,'09-solar-journey');
 // V7: a nappali és az éjszakai napív az egyező háttérképekben van,
 // ezért külön LVGL solar_arc elem nem rajzolhat rájuk újabb vonalat.
 for(const mode of ['light','dark']){
  const elements=solarDoc[mode].elements;
  assert(!elements.some(e=>e.type==='solar_arc'));
  const discs=elements.filter(e=>e.type==='solar_disc');
  assert.strictEqual(discs.length,1);
  assert.strictEqual(discs[0].center_x,474);
  assert.strictEqual(discs[0].center_y,408);
  assert.strictEqual(discs[0].radius_x,386);
  assert.strictEqual(discs[0].radius_y,313);
  assert.strictEqual(discs[0].night_radius_y,186);
  assert.strictEqual(discs[0].day_path.length,21);
  assert.strictEqual(discs[0].night_path.length,21);
  assert.deepStrictEqual(JSON.stringify(discs[0].day_path.at(-1)),JSON.stringify(discs[0].night_path[0]));
  assert.strictEqual(discs[0].night_path[10].y,612);
  const flows=elements.filter(e=>e.type==='flow');
  assert(flows.filter(e=>e.reverse).length===2);
  assert(flows.every(e=>e.dot_size===6&&e.dot_count===2));
  assert(flows.every(e=>e.active_opacity===250&&e.inactive_opacity===184&&e.inactive_glow_opacity===28));
  const bf=flows.find(e=>e.source==='battery');
  // Az energiaút geometriáját a PV-, a ház- és az akkusáv
  // kapcsolódási pontjai alapján ellenőrizzük, nem régi koordinátán.
  assert(bf&&bf.reverse);
  assert(['polyline','bezier'].includes(bf.curve));
  assert(Array.isArray(bf.points)&&bf.points.length>=3);
  const pv=elements.find(e=>e.type==='image'&&e.file==='pv.png');
  const home=elements.find(e=>e.type==='image'&&e.file==='home.png'&&e.y<600);
  const batteryBand=elements.find(e=>e.type==='box'&&e.x===951&&e.y===562);
  assert(pv&&home&&batteryBand);
  const start={x:bf.x+bf.points[0].x,y:bf.y+bf.points[0].y};
  const end={x:bf.x+bf.points.at(-1).x,y:bf.y+bf.points.at(-1).y};
  assert(start.x>=pv.x+pv.w&&start.x<home.x);
  assert(start.y>=pv.y&&start.y<=pv.y+pv.h);
  assert(end.x>=batteryBand.x&&end.x<=batteryBand.x+batteryBand.w);
  assert(end.y<=batteryBand.y&&batteryBand.y-end.y<=8);
  assert(bf.points.every(p=>p.x>=0&&p.y>=0&&p.x<=bf.w&&p.y<=bf.h));
  const batteryPower=elements.find(e=>e.type==='value'&&e.source==='battery');
  assert(batteryPower.font===12&&batteryPower.x===1093);
  assert(batteryPower.positive_color!==batteryPower.negative_color);
  assert(elements.length===94);
  assert(elements.some(e=>e.type==='bar'&&e.source==='soc'));
  assert(elements.find(e=>e.type==='value'&&e.source==='grid').zero_color===
         elements.find(e=>e.type==='value'&&e.source==='pv').color);
  const solarChips=elements.filter(e=>e.type==='box'&&e.y===236);
  assert(solarChips.some(e=>e.x===22)&&solarChips.some(e=>e.x===821));
  const battery=elements.find(e=>e.type==='box'&&e.x===951&&e.y===562);
  assert.strictEqual(elements.filter(e=>e.type==='box'&&e.w===2&&e.h===4&&e.background===battery.outline_color).length,7);
  assert(elements.some(e=>e.type==='image'&&e.file===(mode==='light'?'day.png':'night.png')));
 }
 assert(solarMetrics.encoded>0 && solarMetrics.decoded>0);
 assert(solarFiles.has('preview-light.png')&&solarFiles.has('preview-dark.png'));
 const legacy=await read(process.argv[2]);const doc=JSON.parse(new TextDecoder().decode(legacy.get('template.json')));
 const oldMetric=sandbox.api.validateManifest(doc,legacy);
 legacy.set('preview-light.png',legacy.get('image.png'));
 assert.throws(()=>sandbox.api.validateManifest(doc,legacy));
 legacy.set('preview-dark.png',legacy.get('image.png'));
 assert.strictEqual(sandbox.api.validateManifest(doc,legacy).decoded,oldMetric.decoded);
 let clears=0,draws=0;const bitmap={};
 const canvas={width:480,height:300,getContext:()=>({clearRect:()=>++clears,drawImage:image=>{assert.strictEqual(image,bitmap);++draws;}})};
 sandbox.api.drawStoredPreview(canvas);assert.strictEqual(draws,0); // legacy: empty frame
 sandbox.api.drawStoredPreview(canvas,bitmap);assert.strictEqual(draws,1);assert.strictEqual(clears,2);
 // A valódi oldal kódja: nincs manifest/háttér letöltés, a régi keret üres,
 // ZIP-ből kizárólag a két kicsi előnézet dekódolódhat.
 class Element{
  constructor(tag='div'){this.tag=tag;this.children=[];this.style={};this.isConnected=true;this.draws=0;this.clears=0;}
  append(...nodes){for(const n of nodes){n.parent=this;this.children.push(n);}}
  replaceChildren(...nodes){this.children=[];this.append(...nodes);}
  setAttribute(){}
  remove(){if(this.parent)this.parent.children=this.parent.children.filter(n=>n!==this);}
  getContext(){return {clearRect:()=>++this.clears,drawImage:()=>++this.draws};}
 }
 const elements=new Map(),requests=[],thumbs=[];let decodedImages=0,closedImages=0;
 const document={getElementById:id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);},createElement:tag=>new Element(tag)};
 class ImageStub extends Element{
  set src(url){this.url=url;thumbs.push(this);assert(this.parent,'lazy image must be attached before loading');
   queueMicrotask(()=>url.includes('id=legacy')?this.onerror():this.onload());}
 }
 const page={...sandbox,document,Image:ImageStub,confirm:()=>true,
  fetch:async url=>{requests.push(url);assert.strictEqual(url,'/api/templates');return {ok:true,json:async()=>({active:'builtin',templates:[
   {id:'builtin',name:'Rendszer',version:0},{id:'09-solar-journey',name:'Nap pálya',version:13},{id:'legacy',name:'Régi',version:1}]})};},
  createImageBitmap:async blob=>{const b=new Uint8Array(await blob.arrayBuffer());const size=sandbox.api.pngInfo(b);assert(size.w<=640&&size.h<=400);
   ++decodedImages;return {close:()=>++closedImages};}
 };
 vm.createContext(page);vm.runInContext(fs.readFileSync('assets/web/template-manager.js','utf8'),page);
 await new Promise(resolve=>setImmediate(resolve));
 assert.strictEqual(elements.get('list').children.length,3);
 assert.deepStrictEqual(requests,['/api/templates']);assert.strictEqual(thumbs.length,3);
 assert(thumbs[0].url.includes('id=builtin')&&thumbs[0].loading==='lazy');
 assert(thumbs[1].url.includes('&v=13')&&thumbs[1].loading==='lazy');
 assert.strictEqual(thumbs[2].parent.children.length,0);
 const choose=async path=>{const b=fs.readFileSync(path);elements.get('zip').files=[{arrayBuffer:async()=>b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength)}];await elements.get('zip').onchange();};
 await choose(process.argv[4]);assert.strictEqual(decodedImages,2);
 assert.strictEqual(elements.get('light').draws,1);assert.strictEqual(elements.get('dark').draws,1);
 await choose(process.argv[2]);assert.strictEqual(decodedImages,2);assert.strictEqual(closedImages,2);
 assert.strictEqual(elements.get('light').draws,1);assert.strictEqual(elements.get('light').clears,2);
 assert.strictEqual(elements.get('check').disabled,false); // legacy packages remain installable
 console.log('PASS: saved previews, lazy versioned thumbnails, empty legacy frames and only two image decodes');

 for(const path of process.argv.slice(5)){await assert.rejects(()=>read(path));}
 console.log('PASS: browser real solar journey ZIP, stored/deflate, PNG metadata, CRC, path/duplicate/truncated rejection');
})().catch(e=>{console.error(e);process.exit(1);});
