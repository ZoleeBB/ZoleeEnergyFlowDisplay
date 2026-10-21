/* A kiadott JS valódi mentési útja: három peer, konkurens módosítás és NVS-visszajelzés. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
const nodes = new Map();
class Element {
    constructor(tag) { this.tagName=tag.toUpperCase(); this.children=[]; this._value=''; this.checked=false; this.disabled=false; this.textContent=''; }
    set id(value) { this._id=value; nodes.set(value,this); } get id() { return this._id; }
    set value(value) { this._value=String(value); } get value() { return this._value; }
    setAttribute(key,value) { this[key]=value; }
    append(child) { this.children.push(child); if(this.tagName==='SELECT' && this.children.length===1)this.value=child.value; }
    replaceChildren() { this.children=[]; this.value=''; }
}
for(const id of ['slave-room','slave-category','slave-profiles','slave-status']) {
    const node=new Element(id==='slave-room'||id==='slave-category'?'select':'div'); node.id=id;
}
const id='ble-0-060504030201', id2='ble-0-070504030201', id3='ble-0-080504030201';
const uuid='11223344-5566-7788-99aabbccddeeff00', uuid2='aabbccdd-5566-7788-99aabbccddeeff00';
const catalog=[{uuid,name:'<img src=x> Jelenlét',room:'Nappali',category:'Jelenlét',state:'value'},
    {uuid:uuid2,name:'Mozgás',room:'Kert',category:'Jelenlét',state:'value'}];
const initial=[id,id2,id3].map((id,index)=>({id,name:'Slave '+(index+1),presence_uuid:index?uuid2:uuid,timeout_seconds:30,blank_at_night:false}));
let state={schema:1,enabled:false,ready:false,revision:4294967295,saved_revision:4294967295,save_error:0,profiles:[]};
let offline=false, persist=true, reject=false, afterPostOffline=false, lostResponse=false, dropBeforePost=false, mutateBeforePost=null;
const posts=[];
const sandbox={console,TextEncoder,document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},apiFetch:async(url,options={})=>{
    assert.equal(url,'/api/loxone/slaves');
    if(offline)throw Error('Nincs kapcsolat.');
    if(options.method==='POST') {
        const body=JSON.parse(options.body);
        assert.equal(options.headers['X-Zolee-Loxone'],'1');
        posts.push(body);
        if(dropBeforePost)throw Error('Nem továbbított kérés.');
        if(mutateBeforePost){mutateBeforePost();mutateBeforePost=null;}
        if(reject||body.revision!==state.revision)return {ok:false,status:409,text:async()=>'A profil közben változott.'};
        Object.assign(state.profiles.find(value=>value.id===body.id),body.profile);
        state.revision=(state.revision+1)>>>0||1;
        if(persist)state.saved_revision=state.revision;
        if(afterPostOffline)offline=true;
        if(lostResponse)throw Error('Elveszett válasz.');
        return {ok:true,status:202};
    }
    return {ok:true,json:async()=>structuredClone(state)};
}};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('assets/web/loxone-slaves.js','utf8')+'\nglobalThis.slaves=LoxoneSlaves;',sandbox);
const get=(key,peer=id)=>nodes.get('slave-'+peer+'-'+key);
const edit=(key,value,peer=id)=>{
    const node=get(key,peer); assert(node,key);
    if(key==='blank_at_night')node.checked=value; else node.value=value;
    node.oninput(); return node;
};
const save=(peer=id)=>get('save',peer).onclick();
const refresh=()=>sandbox.slaves.refresh(catalog);
const status=(peer=id)=>get('status',peer).textContent;
(async()=>{
    await refresh(); assert(nodes.get('slave-status').textContent.includes('Kapcsold be'));
    state.enabled=state.ready=true; await refresh(); assert(nodes.get('slave-status').textContent.includes('Még nincs párosított'));
    state.profiles=structuredClone(initial); await refresh();
    assert.equal(nodes.get('slave-profiles').children.length,3);
    assert(get('save').disabled);
    const name=edit('name','<script>csak szöveg</script>');
    edit('name','Háló <b>kijelző</b>',id2);
    edit('timeout_seconds','123',id3);
    await refresh(); assert.equal(get('name'),name); assert.equal(name.value,'<script>csak szöveg</script>');
    nodes.get('slave-room').value=JSON.stringify('Kert');nodes.get('slave-room').onchange();
    assert(get('presence_uuid').children.some(node=>node.value===uuid));
    assert.equal(get('name'),name);
    assert(get('presence_uuid').children.some(node=>node.textContent.includes('<img src=x>')));
    await sandbox.slaves.refresh([]); assert(get('presence_uuid').children.some(node=>node.value===uuid));
    await refresh();
    await save(); assert.equal(posts.length,1);assert.equal(posts[0].revision,4294967295);
    assert.equal(posts[0].profile.name,'<script>csak szöveg</script>');assert(!('password' in posts[0]));
    assert(status().includes('tartósan elmentve'));assert(get('save').disabled);
    assert.equal(get('name',id2).value,'Háló <b>kijelző</b>');
    state.revision++;await refresh();
    assert(status().includes('folyamatban'));
    state.save_error=1;await refresh();assert(status().includes('hibát'));
    state.save_error=0;state.saved_revision=state.revision;await refresh();
    assert(status().includes('tartósan elmentve'));
    /* Másik peer új revíziója nem veszti el a saját draftot. */
    state.profiles[2].name='Másik böngésző';state.revision++;state.saved_revision=state.revision;
    await save(id2);assert.equal(posts.length,2);assert.equal(posts[1].revision,3);
    assert.equal(get('timeout_seconds',id3).value,'123');
    /* A célprofil közbeni módosítása nem írható felül kattintással. */
    edit('name','Saját név');state.profiles[0].name='Távoli név';state.revision++;state.saved_revision=state.revision;
    await refresh();assert.equal(get('name').value,'Saját név');assert(status().includes('másik helyen'));
    await save();assert.equal(posts.length,2);assert(status().includes('közben változott'));
    get('discard').onclick();assert.equal(get('name').value,'Távoli név');assert(get('save').disabled);
    edit('timeout_seconds','1.5');await save();assert.equal(posts.length,2);assert(status().includes('egész'));
    edit('timeout_seconds','86401');await save();assert.equal(posts.length,2);
    edit('timeout_seconds','');await save();assert.equal(posts.length,2);
    edit('timeout_seconds','45');edit('name','é'.repeat(41));await save();assert.equal(posts.length,2);
    edit('name','\u00a0 ');await save();assert.equal(posts.length,2);
    edit('name','név\u0085');await save();assert.equal(posts.length,2);
    edit('name','név\ud800');await save();assert.equal(posts.length,2);
    edit('name','Nappali');edit('presence_uuid','hibás');await save();assert.equal(posts.length,2);
    edit('presence_uuid','');edit('blank_at_night',true);
    /* 202 esetén az UI még vár a háttértask commitjára. */
    persist=false;await save();assert.equal(posts.length,3);assert(status().includes('folyamatban'));
    assert(get('save').disabled && get('name').disabled);assert(!get('name',id2).disabled);
    await refresh();assert(status().includes('folyamatban'));
    state.save_error=123;await refresh();assert(status().includes('sikertelen'));assert(!get('save').disabled && !get('name').disabled);
    edit('timeout_seconds',46);
    state.save_error=0;persist=true;await save();assert.equal(posts.length,4);assert(status().includes('tartósan elmentve'));
    assert.equal(posts.at(-1).profile.presence_uuid,'');assert.equal(posts.at(-1).profile.blank_at_night,true);
    /* NVS háttér-újrapróbálás kattintás nélkül is visszajelezhető. */
    edit('timeout_seconds',50);persist=false;await save();state.save_error=5;await refresh();
    state.save_error=0;state.saved_revision=state.revision;await refresh();assert(status().includes('tartósan elmentve'));
    edit('name','Megőrzött név');offline=true;await refresh();assert.equal(get('name').value,'Megőrzött név');assert(get('save').disabled);
    offline=false;await refresh();assert(!get('save').disabled);
    await sandbox.slaves.refresh(catalog,true);assert(get('save').disabled);await refresh();
    /* A friss GET és POST közti ütközést a backend 409-es válasza fogja meg. */
    mutateBeforePost=()=>{state.profiles[0].name='Ütköző név';state.revision++;};
    const before=posts.length;await save();assert.equal(posts.length,before+1);assert(status().includes('nem igazolt'));
    await refresh();assert.equal(get('name').value,'Megőrzött név');get('discard').onclick();
    /* Elveszett POST-válasz: egyetlen kérés, siker csak ellenőrzött GET után. */
    edit('name','Válasz nélkül');lostResponse=true;persist=true;await save();
    assert.equal(posts.length,before+2);assert(status().includes('tartósan elmentve'));lostResponse=false;
    /* Elfogadott kérés után offline: a draft az újracsatlakozásig megmarad. */
    edit('name','Offline után');afterPostOffline=true;await save();assert(get('save').disabled);
    assert.equal(get('name').value,'Offline után');afterPostOffline=false;offline=false;await refresh();assert(status().includes('tartósan elmentve'));
    edit('name','El sem küldött');dropBeforePost=true;await save();
    assert(status().includes('korábbi beállítást'));assert(!get('save').disabled);
    dropBeforePost=false;await save();assert(status().includes('tartósan elmentve'));
    /* Eltűnő eszköznél nincs tetszőleges identitásnak küldött mentés. */
    edit('name','Utolsó draft');state.profiles=state.profiles.slice(1);await refresh();
    assert.equal(get('name').value,'Utolsó draft');assert(get('save').disabled);
    console.log('PASS: actual Slave editor, 3 identities, edit/node preservation, filters, safe text, strict validation, optimistic conflict, uint32 revision, accepted/durable/error/retry and ambiguous response recovery');
})().catch(error=>{console.error(error);process.exitCode=1;});
