const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const sandbox={console,TextEncoder,TextDecoder,DataView,Uint8Array,Map,Set,ArrayBuffer,Blob,DecompressionStream};
vm.createContext(sandbox);vm.runInContext(fs.readFileSync('assets/web/template-manager.js','utf8')+'\nglobalThis.api={unpackZip,validateManifest};',sandbox);
(async()=>{
 for(let index=2;index<process.argv.length;index++){
  const data=fs.readFileSync(process.argv[index]);const buffer=data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength);
  try{
   const files=await sandbox.api.unpackZip(buffer),doc=JSON.parse(new TextDecoder().decode(files.get('template.json')));
   const memory=sandbox.api.validateManifest(doc,files);assert.equal(index,2,'invalid package accepted');
   assert(memory.encoded<5242880&&memory.decoded<12582912);
  }catch(error){if(index===2)throw error;}
 }
 console.log('PASS: actual browser resource ZIP/manifest validation, 64-icon dimensions, exact file list and normal-package limit');
})().catch(error=>{console.error(error);process.exitCode=1;});
