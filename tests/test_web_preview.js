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
 // V6: a nappali és az éjszakai napív az egyező háttérképekben van,
 // ezért külön LVGL solar_arc elem nem rajzolhat rájuk újabb vonalat.
 for(const mode of ['light','dark']){
  const elements=solarDoc[mode].elements;
  assert(!elements.some(e=>e.type==='solar_arc'));
  const discs=elements.filter(e=>e.type==='solar_disc');
  assert.strictEqual(discs.length,1);
  assert.strictEqual(discs[0].center_x,436);
  assert.strictEqual(discs[0].center_y,383);
  assert.strictEqual(discs[0].radius_x,369);
  assert.strictEqual(discs[0].radius_y,289);
  assert.strictEqual(discs[0].night_radius_y,208);
  assert(elements.some(e=>e.type==='image'&&e.file===(mode==='light'?'day.png':'night.png')));
 }
 assert(solarMetrics.encoded>0 && solarMetrics.decoded>0);
 for(const path of process.argv.slice(5)){await assert.rejects(()=>read(path));}
 console.log('PASS: browser real solar journey ZIP, stored/deflate, PNG metadata, CRC, path/duplicate/truncated rejection');
})().catch(e=>{console.error(e);process.exit(1);});
