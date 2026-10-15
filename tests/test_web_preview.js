/* Runs the exact browser ZIP reader with real stored/deflate archives. */
const fs=require('fs'),vm=require('vm'),assert=require('assert');
const sandbox={DecompressionStream,Blob,TextDecoder,TextEncoder,Uint8Array,DataView,Map,console};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('assets/web/template-manager.js','utf8')+'\nglobalThis.api={unpackZip,validateManifest,pngInfo};',sandbox);
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
  assert(bf.curve==='bezier'&&bf.reverse&&bf.x===1036&&bf.y===472);
  assert(bf.points[0].x===40&&bf.points.at(-1).y===86);
  const batteryPower=elements.find(e=>e.type==='value'&&e.source==='battery');
  assert(batteryPower.font===12&&batteryPower.x===1093);
  assert(batteryPower.positive_color!==batteryPower.negative_color);
  assert(elements.length===89);
  const solarChips=elements.filter(e=>e.type==='box'&&e.y===236);
  assert(solarChips.some(e=>e.x===26)&&solarChips.some(e=>e.x===821));
  const battery=elements.find(e=>e.type==='box'&&e.x===951&&e.y===562);
  assert.strictEqual(elements.filter(e=>e.type==='box'&&e.w===2&&e.h===4&&e.background===battery.outline_color).length,7);
  assert(elements.some(e=>e.type==='image'&&e.file===(mode==='light'?'day.png':'night.png')));
 }
 assert(solarMetrics.encoded>0 && solarMetrics.decoded>0);
 for(const path of process.argv.slice(5)){await assert.rejects(()=>read(path));}
 console.log('PASS: browser real solar journey ZIP, stored/deflate, PNG metadata, CRC, path/duplicate/truncated rejection');
})().catch(e=>{console.error(e);process.exit(1);});
