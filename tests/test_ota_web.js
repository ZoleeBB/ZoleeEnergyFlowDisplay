/* A firmware-be ágyazott tényleges JS lefut, nem külön tesztmásolat. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
(async () => {
  const elements = Object.fromEntries(['auto','btn','f','s'].map(id => [id,{disabled:false,textContent:''}]));
  let pending;
  const calls = [];
  const context = {
    document:{getElementById:id=>elements[id]},
    fetch:(url,options)=>{calls.push({url,options});return new Promise(resolve=>{pending=resolve;});},
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(process.argv[2], 'utf8'), context);
  const run = context.autoNow();
  assert.equal(elements.auto.disabled, true);
  assert.equal(elements.btn.disabled, true);
  assert.equal(elements.f.disabled, true);
  assert.equal(calls[0].url, '/api/ota/auto');
  assert.equal(calls[0].options.method, 'POST');
  assert.equal(calls[0].options.headers['X-Zolee-OTA'], '1');
  pending({status:202, text:async()=> 'accepted'});
  await run;
  assert.equal(elements.s.textContent, 'accepted');
  assert.equal(elements.auto.disabled, false);
  const busy = context.autoNow();
  pending({status:409, text:async()=> 'busy'});
  await busy;
  assert.equal(elements.s.textContent, 'busy');
  context.fetch = async()=>{throw Error('offline');};
  await context.autoNow();
  assert(elements.s.textContent.length > 0);
  assert.equal(elements.auto.disabled, false);
  console.log('PASS: actual OTA button JS, request header, busy/error feedback and controls');
})().catch(error=>{console.error(error);process.exitCode=1;});
