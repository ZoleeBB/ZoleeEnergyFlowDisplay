/* A firmware által kiírt tényleges szkript: lekérés, megerősítés, nullázás. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
const html = fs.readFileSync(process.argv[2], 'utf8');
const nodes = new Map();
const node = () => ({textContent:'', disabled:false, children:[], append(child){this.children.push(child);},replaceChildren(){this.children=[];}});
for (const id of ['battery-title','battery-note','battery-values','battery-reset','battery-feedback']) nodes.set(id,node());
let confirmation = false, posts = 0, fail = false;
const sandbox = {console, Number, Error, document:{getElementById:id=>nodes.get(id),createElement:node},
    confirm:()=>confirmation,setInterval:()=>{},fetch:async(url, options={})=>{
        if(options.method==='POST') { assert.equal(url,'/api/battery/reset');assert.equal(options.headers['X-Zolee-Battery'],'1');posts++;
            return {ok:!fail,text:async()=>'SD hiba'}; }
        return {ok:true,json:async()=>({local:true,available:true,nominal_kwh:15,charge_kwh:3,discharge_kwh:2,cycles:0.2,runs:0,active_direction:0})};
    }};
(async()=>{
    vm.createContext(sandbox);vm.runInContext(html.match(/<script>([\s\S]*)<\/script>/)[1],sandbox);
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(nodes.get('battery-values').children.length,8);
    assert.equal(nodes.get('battery-reset').disabled,false);
    await nodes.get('battery-reset').onclick();assert.equal(posts,0);
    confirmation=true;await nodes.get('battery-reset').onclick();assert.equal(posts,1);
    assert.match(nodes.get('battery-feedback').textContent,/elmentve/);
    fail=true;await nodes.get('battery-reset').onclick();assert.equal(posts,2);
    assert.equal(nodes.get('battery-feedback').textContent,'SD hiba');assert.equal(nodes.get('battery-reset').disabled,false);
    console.log('PASS: rendered battery section, missing estimates, confirmation, successful reset and server error');
})().catch(error=>{console.error(error);process.exitCode=1;});
