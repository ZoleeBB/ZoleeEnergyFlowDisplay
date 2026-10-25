/* A kiadott szerkesztő valódi JS-e: szerkesztés, szűrés, mentés és hibák. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
const nodes = new Map();
class Element {
    constructor(tag) { this.tagName=tag.toUpperCase();this.children=[];this._value='';this.checked=false;this.disabled=false;this.textContent=''; }
    set id(value) { this._id=value;nodes.set(value,this); } get id() { return this._id; }
    set value(value) { this._value=String(value); } get value() { return this._value; }
    append(child) { child.parentNode=this;this.children.push(child);if(this.tagName==='SELECT'&&this.children.length===1)this.value=child.value; }
    setAttribute(key,value) { this[key]=value; }
    removeAttribute(key) { delete this[key]; }
    replaceChildren() { this.children=[];this.value=''; }
}
for(const id of ['ringtone-file','ringtone-upload','ringtone-status','consumers-save','consumers-status','consumers-section','intercom-config','intercom-save','intercom-status','home-resource-warning','home-pages','home-add-page','home-editor','home-status','home-save','home-add-consumer','home-add-card','home-consumers','home-cards','home-room','home-category']) {
    const node=new Element(id.includes('room')||id.includes('category')?'select':'div');node.id=id;
}
const icons=JSON.parse(fs.readFileSync('assets/loxone-home/catalog.json')).icons.map(({id,hu,en})=>({id,hu,en}));
const uuid='11223344-5566-7788-99aabbccddeeff00',control='aabbccdd-5566-7788-99aabbccddeeff00';
const catalog=[{uuid,control_uuid:control,name:'<img src=x> Hőmérő',room:'Nappali',category:'Klíma',state:'value',type:'Switch',secured:false},
    {uuid:control,name:'Csak állapot',room:'Kert',category:'Kert',state:'value'},
    {uuid,control_uuid:'dddddddd-5566-7788-99aabbccddeeff00',action_uuid:'cccccccc-5566-7788-99aabbccddeeff00',name:'Külön parancscím',type:'Switch',secured:false},
    {uuid,control_uuid:'eeeeeeee-5566-7788-99aabbccddeeff00',action_uuid:'',name:'Hibás parancscím',type:'Switch',secured:false}];
let state={schema:1,consumers:[],cards:[],busy:false,revision:0,status:'',resources_installed:false,resources_version:0}, reject=false, offline=false, hold=false;
const posts=[];
let uploadAbort=false, uploads=0, transferBusy=false;
let uploadReport = false;
const sandbox={console,TextEncoder,URL,setLoxoneTransferBusy:active=>{transferBusy=active;},document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},apiFetch:async(url,options={},timeoutMs)=>{
    if(url==='/api/loxone/intercom/ringtones') {
        assert.equal(transferBusy,true);
        assert.equal(timeoutMs,180000); assert.equal(options.method,'POST'); uploads++;
        if(uploadAbort) throw Object.assign(Error('signal is aborted without reason'),{name:'AbortError'});
        return {ok:true,text:async()=>uploadReport ? JSON.stringify({installed:1, failed:[{name:'broken.wav', reason:'CRC-hiba'}], complete:true, error:''}) : 'Telepítve.'};
    }
    assert.equal(url,'/api/loxone/home');
    if(offline)throw Error('kapcsolat megszakadt');
    if(options.method==='POST') {
        if(reject)return {ok:false,text:async()=>'Az SD-kártya nem írható.'};
        assert.equal(options.headers['X-Zolee-Loxone'],'1');posts.push(JSON.parse(options.body));
        state.busy=hold;
        if(!hold){state={...state,...JSON.parse(options.body),revision:state.revision+1,status:'Otthon-beállítások elmentve.'};state.intercom.has_video_password=true;delete state.intercom.video_password;}
        return {ok:true};
    }
    return {ok:true,json:async()=>structuredClone(state)};
}};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',sandbox);
const edit=(id,value,event='oninput')=>{const node=nodes.get(id);assert(node,id);node.value=value;node[event]();return node;};
const click=id=>nodes.get(id).onclick();
(async()=>{
    assert(nodes.get('home-save').disabled);
    await sandbox.home.refresh(catalog);assert(!nodes.get('home-save').disabled);
    edit('intercom-speaker-volume', '73');
    edit('intercom-microphone-gain', '90');
    assert.equal(nodes.get('intercom-ringtone-preview').parentNode.hidden,true);
    assert(!nodes.has('intercom-speaker-test'));
    assert.equal(nodes.get('home-resource-warning').hidden,false);
    const enabled=nodes.get('intercom-enabled'); enabled.checked=true;enabled.onchange();
    await click('intercom-save');assert.equal(posts.length,0);
    edit('intercom-stream-url',' \u00a0http://192.168.2.21/mjpg/video.mjpg\u00a0 ');
    edit('intercom-video-username','admin');
    edit('intercom-video-password','Camera-secret');
    edit('intercom-bell',uuid,'onchange');
    edit('intercom-gate',control,'onchange');
    edit('intercom-gate-name','Kapu nyitása');
    await click('intercom-save');assert.equal(posts.length,1);
    assert.equal(posts[0].intercom.stream_url,'http://192.168.2.21/mjpg/video.mjpg');
    assert.equal(posts[0].intercom.speaker_volume,73);
    assert.equal(posts[0].intercom.microphone_gain,90);
    assert.equal(nodes.get('intercom-stream-url').value,posts[0].intercom.stream_url);
    assert.equal(posts[0].intercom.gate.type,'pulse');
    assert.equal(posts[0].intercom.gate.control_uuid,control);
    assert.equal(posts[0].intercom.gate.name,'Kapu nyitása');
    assert(!('username' in posts[0].intercom) && !('password' in posts[0].intercom));
    await sandbox.home.refresh(catalog);
    assert.equal(nodes.get('intercom-gate-name').value,'Kapu nyitása');
    edit('intercom-stream-url','http://user:password@camera/video');
    await click('intercom-save');assert.equal(posts.length,1);
    edit('intercom-stream-url','http://192.168.1.50/mjpg/video.mjpg');
    edit('intercom-gate','','onchange');
    assert(!nodes.get('intercom-gate').value);
    posts.length=0;
        click('home-add-consumer');assert.equal(nodes.get('home-consumers-item-1-icon').children.length,64);
    assert(!nodes.has('home-consumers-item-1-icon-preview'));
    state.resources_installed=true;state.resources_version=3;await sandbox.home.refresh(catalog);
    assert.equal(nodes.get('home-resource-warning').hidden,true);
    const preview=nodes.get('home-consumers-item-1-icon-preview');assert(preview.src.includes('/api/templates/home-icon?id='));
    edit('home-consumers-item-1-icon','washing-machine','onchange');assert(preview.src.includes('id=washing-machine&v=3'));
    assert.equal(preview.width,64);assert.equal(preview.height,64);
    await click('consumers-save');assert.equal(posts.length,0);assert(nodes.get('consumers-status').textContent.includes('nevet'));
    edit('home-consumers-item-1-name','Mosógép');edit('home-consumers-item-1-uuid',uuid,'onchange');edit('home-consumers-item-1-scale','.001');
    await click('consumers-save');assert.equal(posts.length,1);
    assert.equal(posts[0].consumers[0].source.scale,.001);assert.equal(posts[0].consumers[0].average_kw,null);
    assert.equal(posts[0].consumers[0].suggest_start,false);assert(!('password' in posts[0]));
    edit('home-consumers-item-1-name','Még nincs mentve');
    await sandbox.home.refresh(catalog);assert.equal(nodes.get('home-consumers-item-1-name').value,'Még nincs mentve');
    edit('home-room',JSON.stringify('Kert'),'onchange');
    assert(nodes.get('home-consumers-item-1-uuid').children.some(node=>node.value===uuid));
    assert.equal(nodes.get('home-consumers-item-1-name').value,'Még nincs mentve');
    edit('home-consumers-item-1-average_kw','-1');await click('consumers-save');assert.equal(posts.length,1);
    edit('home-consumers-item-1-average_kw','2.1');const suggest=nodes.get('home-consumers-item-1-suggest_start');suggest.checked=true;suggest.onchange();
    click('home-add-page'); edit('home-page-title','Földszint');
    click('home-add-card');
    edit('home-cards-item-1-type','pulse','onchange');
    assert(nodes.get('home-consumers-item-1-name') !== nodes.get('home-cards-item-1-name'));
    /* A két oldal azonos elemkulcsai külön DOM-névtérben élnek. */
    const target=nodes.get('home-cards-item-1-control_uuid');
    assert(!target.children.some(node=>node.value===uuid));
    edit('home-room','','onchange');
    assert(nodes.get('home-cards-item-1-control_uuid').children.some(node=>node.value===control));
    const targets=nodes.get('home-cards-item-1-control_uuid').children.map(node=>node.value);
    assert(targets.includes('dddddddd-5566-7788-99aabbccddeeff00'));
    assert(!targets.includes('cccccccc-5566-7788-99aabbccddeeff00'));
    assert(!targets.includes('eeeeeeee-5566-7788-99aabbccddeeff00'));
    edit('home-cards-item-1-name','Kapunyitás');edit('home-cards-item-1-control_uuid',control,'onchange');
    await click('home-save');assert.equal(posts.length,2);assert.equal(posts[1].pages[0].cards[0].type,'pulse');
    assert.equal(posts[1].pages[0].cards[0].control_uuid,control);assert(!('consumers' in posts[1]));assert(!('intercom' in posts[1]));assert.equal(posts[1].section,'pages');
    edit('home-cards-item-1-type','switch','onchange');edit('home-cards-item-1-uuid',uuid,'onchange');
    assert(nodes.get('home-cards-item-1-scale').disabled);
    await click('home-save');assert.equal(posts.length,3);assert.equal(posts[2].pages[0].cards[0].source.scale,1);
    reject=true;await click('home-save');assert(nodes.get('home-status').textContent.includes('nem írható'));reject=false;
    offline=true;await sandbox.home.refresh(catalog);assert(nodes.get('home-status').textContent.includes('megszakadt'));offline=false;
    hold=true;await click('home-save');assert(nodes.get('home-save').disabled);assert(nodes.get('home-add-card').disabled);
    state.busy=false;state.status='Az SD-mentés sikertelen.';await sandbox.home.refresh(catalog);
    assert(!nodes.get('home-save').disabled);assert.equal(nodes.get('home-status').className,'feedback-error');
    /* Háttérből érkező másik böngészőmódosítás sem írja felül a helyi szerkesztést. */
    edit('home-cards-item-1-name','<script>alert(1)</script>');state.cards=[];state.revision++;
    await sandbox.home.refresh([...catalog,{uuid:'eeeeeeee-5566-7788-99aabbccddeeff00',name:'Új érték'}]);
    assert.equal(nodes.get('home-cards-item-1-name').value,'<script>alert(1)</script>');
    /* A firmware a pulse-nál csak a releváns mezőket szerializálja.
       Új oldalletöltés után pulse -> érték váltásnál is legyenek jó alapértékek. */
    state={schema:1,consumers:[],cards:[{id:'saved-pulse',name:'Mentett gomb',icon:icons[0].id,type:'pulse',control_uuid:control}],busy:false,revision:0,status:''};
    hold=false;
    const secondContext={...sandbox};vm.createContext(secondContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',secondContext);
    await secondContext.home.refresh(catalog);
    edit('home-cards-saved-pulse-type','value','onchange');
    edit('home-cards-saved-pulse-uuid',uuid,'onchange');
    const before=posts.length;await click('home-save');assert.equal(posts.length,before+1);
    assert.equal(posts.at(-1).pages[0].cards[0].warning_low,null);assert.equal(posts.at(-1).pages[0].cards[0].decimals,1);
    for(let i=0;i<13;i++)click('home-add-consumer');
    assert.equal(nodes.get('home-consumers').children.length,12);assert(nodes.get('home-add-consumer').disabled);
    const firstBox=nodes.get('home-consumers').children[0];firstBox.children.at(-1).onclick();
    assert.equal(nodes.get('home-consumers').children.length,11);assert(!nodes.get('home-add-consumer').disabled);
    /* Több oldal, címek és kártyák megmaradnak lapváltás és átrendezés után. */
    click('home-add-page'); edit('home-page-title','Kert <b>fények</b>');
    assert.equal(nodes.get('home-cards').children.length,0);
    click('home-add-card'); edit('home-cards-item-1-name','Kerti gomb');
    edit('home-cards-item-1-type','pulse','onchange');
    edit('home-cards-item-1-control_uuid',control,'onchange');
    edit('home-page-select','home','onchange');
    assert.equal(nodes.get('home-cards-saved-pulse-name').value,'Mentett gomb');
    edit('home-page-select','page-1','onchange');
    assert.equal(nodes.get('home-page-title').value,'Kert <b>fények</b>');
    click('home-page-up'); assert.equal(nodes.get('home-page-select').value,'page-1');
    await click('home-save');
    /* A névtelen fogyasztók nem akadályozhatják az Otthon-oldalak külön mentését. */
    while(nodes.get('home-consumers').children.length) nodes.get('home-consumers').children[0].children.at(-1).onclick();
    await click('home-save');
    assert.equal(posts.at(-1).schema,2);assert.equal(posts.at(-1).pages[0].title,'Kert <b>fények</b>');
    assert.equal(posts.at(-1).pages[0].cards[0].name,'Kerti gomb');
    for(let i=0;i<12;i++)click('home-add-card');
    assert.equal(nodes.get('home-cards').children.length,12);assert(nodes.get('home-add-card').disabled);
    for(let i=0;i<9;i++)click('home-add-page');
    assert(nodes.get('home-add-page').disabled);assert.equal(nodes.get('home-page-select').children.length,8);
    click('home-remove-page');assert(!nodes.get('home-add-page').disabled);
    await sandbox.home.refresh(catalog,false,true);assert.equal(nodes.get('consumers-section').hidden,true);
    await sandbox.home.refresh(catalog,false);assert.equal(nodes.get('consumers-section').hidden,true);
    await sandbox.home.refresh(catalog,false,false);assert.equal(nodes.get('consumers-section').hidden,false);
    nodes.get('ringtone-file').files=[{name:'private.zip',size:2716219}];
    await nodes.get('ringtone-upload').onclick();
    assert.equal(uploads,1); assert.equal(transferBusy,false); assert.equal(nodes.get('ringtone-status').textContent,'Telepítve.');
    uploadReport=true;
    await click('ringtone-upload');
    assert.equal(uploads,2); assert.match(nodes.get('ringtone-status').textContent,/Telepített hangok: 1/);
    assert.match(nodes.get('ringtone-status').textContent,/broken.wav: CRC-hiba/);
    uploadAbort=true;
    await nodes.get('ringtone-upload').onclick();
    assert.equal(uploads,3); assert.equal(transferBusy,false); assert.match(nodes.get('ringtone-status').textContent,/3 perces/);
    assert.equal(nodes.get('ringtone-upload').disabled,false);
    // A valódi fetch-segéd normál kérései továbbra is 15 másodpercesek.
    const fetchSource=fs.readFileSync('assets/web/loxone.js','utf8');
    const timers=[];
    const fetchContext={AbortController,setTimeout:(fn,ms)=>{timers.push(ms);return 1;},clearTimeout:()=>{},fetch:async()=>({ok:true})};
    vm.createContext(fetchContext);
    vm.runInContext(fetchSource.slice(fetchSource.indexOf('async function apiFetch('),fetchSource.indexOf('function validate(')),fetchContext);
    await fetchContext.apiFetch('/api/loxone');
    await fetchContext.apiFetch('/upload',{},180000);
    assert.deepEqual(timers,[15000,180000]);
    console.log('PASS: actual home editor, 64 icons, consumer transforms, explicit command target, pulse/switch, filters retaining mappings, pending/failure states, separate video credentials, retained blank password, no integration secret or background edit loss');
})().catch(error=>{console.error(error);process.exitCode=1;});
