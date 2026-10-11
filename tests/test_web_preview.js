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
 for(const path of process.argv.slice(4)){await assert.rejects(()=>read(path));}
 console.log('PASS: browser stored/deflate ZIP, PNG metadata, CRC, path/duplicate/truncated rejection');
})().catch(e=>{console.error(e);process.exit(1);});
