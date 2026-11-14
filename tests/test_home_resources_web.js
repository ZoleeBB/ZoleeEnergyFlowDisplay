/* A kiadott böngésző ZIP-/sémaellenőrzője ugyanazokat a határokat vizsgálja, mint a C-telepítő. */
const fs = require('fs'), vm = require('vm'), assert = require('assert/strict');
const sandbox = {console, TextEncoder, TextDecoder, DataView, Uint8Array, Map, Set, ArrayBuffer, Blob, DecompressionStream};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('assets/web/template-manager.js', 'utf8') + '\nglobalThis.api={unpackZip,validateManifest};', sandbox);
(async () => {
    const cases = [{name:'real-v4',path:process.argv[2],operation:'probe',valid:true},
        ...JSON.parse(fs.readFileSync(process.argv[3], 'utf8'))];
    for (const test of cases) {
        const data = fs.readFileSync(test.path);
        const buffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
        let accepted = false, memory, failure;
        try {
            const files = await sandbox.api.unpackZip(buffer);
            if (test.operation !== 'unzip') {
                const doc = JSON.parse(new TextDecoder().decode(files.get('template.json')));
                memory = sandbox.api.validateManifest(doc, files);
            }
            accepted = true;
        } catch (error) { failure = error; }
        assert.equal(accepted, test.valid, test.name + ': ' + (failure?.message || 'invalid package accepted'));
        if (test.valid && memory) {
            if (test.encoded !== undefined) assert.equal(memory.encoded, test.encoded, test.name);
            if (test.decoded !== undefined) assert.equal(memory.decoded, test.decoded, test.name);
            assert(memory.encoded <= 6 * 1024 * 1024 && memory.decoded <= 20 * 1024 * 1024);
        }
    }
    console.log('PASS: actual browser v4 ZIP/manifest validation, ' + cases.length + ' real/legacy/boundary cases, skin pair and dimensions, exact file list, separate ordinary-theme limits');
})().catch(error => {console.error(error); process.exitCode = 1;});
