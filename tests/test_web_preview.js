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
 assert(solarDoc.light.elements.some(e=>e.type==='solar_disc'));
 assert(solarDoc.dark.elements.some(e=>e.type==='solar_arc'));
 assert(solarMetrics.encoded>0 && solarMetrics.decoded>0);
 for(const path of process.argv.slice(5)){await assert.rejects(()=>read(path));}
 console.log('PASS: browser real solar journey ZIP, stored/deflate, PNG metadata, CRC, path/duplicate/truncated rejection');
})().catch(e=>{console.error(e);process.exit(1);});
