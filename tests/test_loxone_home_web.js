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
    focus() { sandbox.document.activeElement=this; }
    setSelectionRange(start,end) { this.selectionStart=start;this.selectionEnd=end; }
    scrollIntoView(options) { this.lastScroll=options; }
}
for(const id of ['ringtone-file','ringtone-upload','ringtone-status','consumers-save','consumers-status','consumers-section','intercom-config','intercom-save','intercom-status','home-resource-warning','home-pages','home-page-tabs','home-page-panel','home-pages-empty','home-add-page','home-editor','home-status','home-save','home-add-consumer','home-add-card','home-consumers','home-cards','home-room','home-category']) {
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
const pageTabs=()=>nodes.get('home-page-tabs').children;
const activeTab=()=>pageTabs().find(tab=>tab['aria-selected']==='true');
const key=(id,value)=>{let prevented=false;nodes.get(id).onkeydown({key:value,preventDefault(){prevented=true;}});return prevented;};
(async()=>{
    assert(nodes.get('home-save').disabled);
    await sandbox.home.refresh(catalog);assert(!nodes.get('home-save').disabled);
    assert.equal(pageTabs().length,0);assert(nodes.get('home-page-panel').hidden);
    assert.equal(nodes.get('home-pages-empty').hidden,false);assert.equal(nodes.get('home-add-page').hidden,false);
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
        click('home-add-consumer');assert.equal(nodes.get('home-consumers-item-1-icon').children.length,icons.length);
    assert(!nodes.has('home-consumers-item-1-icon-preview'));
    state.resources_installed=true;state.resources_version=3;await sandbox.home.refresh(catalog);
    assert.equal(nodes.get('home-resource-warning').hidden,true);
    const preview=nodes.get('home-consumers-item-1-icon-preview');assert(preview.src.includes('/api/templates/home-icon?id='));
    edit('home-consumers-item-1-icon','washing-machine','onchange');assert(preview.src.includes('id=washing-machine&v=3'));
    assert.equal(preview.width,80);assert.equal(preview.height,80);
    const options=nodes.get('home-consumers-item-1-icon').children.map(node=>node.textContent);
    assert.deepEqual(options,[...options].sort((a,b)=>a.localeCompare(b,'hu',{sensitivity:'base'})));
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
    assert.equal(activeTab().id,'home-page-tab-page-1');assert.equal(activeTab().tabIndex,0);
    assert.equal(activeTab().children[0].textContent,'Földszint');assert.equal(activeTab().children[1].textContent,'0/12');
    assert.equal(nodes.get('home-page-panel').hidden,false);assert(nodes.get('home-pages-empty').hidden);
    assert(nodes.get('home-page-up').disabled && nodes.get('home-page-down').disabled);
    assert.equal(sandbox.document.activeElement.id,'home-page-title');
    click('home-add-card');
    assert.equal(activeTab().children[1].textContent,'1/12');
    const freshBox=nodes.get('home-cards').children[0];
    assert.equal(nodes.get('home-cards-item-1-type').value,'');
    assert.equal(freshBox.children.length,3); /* Legenda, típus, mozgatás/törlés. */
    const beforeType=posts.length;await click('home-save');
    assert.equal(posts.length,beforeType);assert.match(nodes.get('home-status').textContent,/kártyatípust/);
    assert.equal(nodes.get('home-cards-item-1-move-up').disabled,true);
    assert.equal(nodes.get('home-cards-item-1-move-down').disabled,true);
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
    assert(pageTabs().every(tab=>tab.disabled));assert(nodes.get('home-add-page').disabled);
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
    const pageTitle=nodes.get('home-page-title'), sameTab=activeTab();
    pageTitle.focus();pageTitle.setSelectionRange(4,9);
    edit('home-page-title','Kert <b>fények</b>!');
    assert.equal(nodes.get('home-page-title'),pageTitle);assert.equal(activeTab(),sameTab);
    assert.equal(activeTab().children[0].textContent,'Kert <b>fények</b>!');
    assert.equal(sandbox.document.activeElement,pageTitle);assert.equal(pageTitle.selectionStart,4);
    edit('home-page-title','');assert.equal(activeTab().children[0].textContent,'Új oldal');
    edit('home-page-title','Kert <b>fények</b>');
    assert.equal(nodes.get('home-cards').children.length,0);
    click('home-add-card'); edit('home-cards-item-1-type','pulse','onchange');
    edit('home-cards-item-1-name','Kerti gomb');
    edit('home-cards-item-1-control_uuid',control,'onchange');
    click('home-page-tab-home');
    assert.equal(nodes.get('home-cards-saved-pulse-name').value,'Mentett gomb');
    click('home-page-tab-page-1');
    assert.equal(nodes.get('home-page-title').value,'Kert <b>fények</b>');
    assert.equal(sandbox.document.activeElement.id,'home-page-tab-page-1');
    assert.equal(activeTab()['aria-controls'],'home-page-panel');
    assert.equal(pageTabs().filter(tab=>tab.tabIndex===0).length,1);
    assert(key(activeTab().id,'Home'));assert.equal(activeTab().id,'home-page-tab-home');
    assert(key(activeTab().id,'ArrowLeft'));assert.equal(activeTab().id,'home-page-tab-page-1');
    assert(key(activeTab().id,'ArrowRight'));assert.equal(activeTab().id,'home-page-tab-home');
    assert(key(activeTab().id,'End'));assert.equal(activeTab().id,'home-page-tab-page-1');
    assert(!key(activeTab().id,'ArrowDown'));assert.equal(activeTab().id,'home-page-tab-page-1');
    assert.equal(nodes.get('home-cards-item-1-name').value,'Kerti gomb');
    hold=true;await click('home-save');
    assert(pageTabs().every(tab=>tab.disabled));assert(nodes.get('home-page-title').disabled);
    click('home-page-tab-home');assert.equal(activeTab().id,'home-page-tab-page-1');
    assert(!key(activeTab().id,'Home'));assert.equal(activeTab().id,'home-page-tab-page-1');
    assert(nodes.get('home-add-page').disabled && nodes.get('home-remove-page').disabled);
    hold=false;state.busy=false;state.revision++;state.status='Elmentve.';
    await secondContext.home.refresh(catalog);
    assert(!activeTab().disabled);assert.equal(nodes.get('home-cards-item-1-name').value,'Kerti gomb');
    click('home-page-up'); assert.equal(nodes.get('home-page-panel')['aria-labelledby'],'home-page-tab-page-1');
    assert.deepEqual(pageTabs().map(tab=>tab.id),['home-page-tab-page-1','home-page-tab-home']);
    assert.equal(sandbox.document.activeElement.id,'home-page-down');
    assert.equal(activeTab().children[0].textContent,'Kert <b>fények</b>');
    await click('home-save');
    /* A névtelen fogyasztók nem akadályozhatják az Otthon-oldalak külön mentését. */
    while(nodes.get('home-consumers').children.length) nodes.get('home-consumers').children[0].children.at(-1).onclick();
    await click('home-save');
    assert.equal(posts.at(-1).schema,2);assert.equal(posts.at(-1).pages[0].title,'Kert <b>fények</b>');
    assert.equal(posts.at(-1).pages[0].cards[0].name,'Kerti gomb');
    for(let i=0;i<12;i++)click('home-add-card');
    assert.equal(nodes.get('home-cards').children.length,12);assert(nodes.get('home-add-card').disabled);
    for(let i=0;i<9;i++)click('home-add-page');
    assert(nodes.get('home-add-page').disabled && nodes.get('home-add-page').hidden);assert.equal(pageTabs().length,8);
    click('home-remove-page');assert(!nodes.get('home-add-page').disabled && !nodes.get('home-add-page').hidden);
    assert.equal(sandbox.document.activeElement,activeTab());
    while(pageTabs().length)click('home-remove-page');
    assert(nodes.get('home-page-panel').hidden && nodes.get('home-page-tabs').hidden);
    assert.equal(nodes.get('home-pages-empty').hidden,false);assert.equal(sandbox.document.activeElement.id,'home-add-page');
    click('home-add-page');assert.equal(pageTabs().length,1);assert.equal(activeTab().id,'home-page-tab-page-1');
    console.log('PASS: page tabs, live safe titles without DOM replacement, keyboard/focus, unsaved fields, page order, busy guards and 0/1/8-page limits');
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
    /* A világításkártyához csak szülőblokk kell, külön visszajelző UUID nem. */
    const lightControl = '10000000-0000-0000-0000000000000001';
    const lightCatalog = [...catalog, {uuid:'10000000-0000-0000-0000000000000004',
        control_uuid:lightControl, action_uuid:lightControl, name:'Nappali <b>világítás</b>',
        type:'LightControllerV2', secured:false, room:'Nappali', category:'Világítás', lighting:'{}'}];
    state = {schema:2, consumers:[], pages:[{id:'lighting-page', title:'Világítás', cards:[{
        id:'lighting-test', name:'Nappali világítás', icon:icons[0].id, type:'lighting',
        control_uuid:lightControl}]}], intercom:{}, revision:0, busy:false, status:''};
    const lightingContext = {...sandbox}; vm.createContext(lightingContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',lightingContext);
    await lightingContext.home.refresh(lightCatalog);
    const lightingSelect = nodes.get('home-cards-lighting-test-control_uuid');
    assert(lightingSelect.children.some(option => option.value === lightControl));
    assert(!lightingSelect.children.some(option => option.value === control));
    assert(!nodes.has('home-cards-lighting-test-uuid'));
    assert(nodes.get('home-cards-lighting-test-type').children.some(option => option.value === 'lighting'));
    const lightingPosts = posts.length;
    await click('home-save');
    assert.equal(posts.length, lightingPosts + 1);
    assert.equal(posts.at(-1).pages[0].cards[0].type, 'lighting');
    assert.equal(posts.at(-1).pages[0].cards[0].control_uuid, lightControl);
    edit('home-room', JSON.stringify('Kert'), 'onchange');
    assert(nodes.get('home-cards-lighting-test-control_uuid').children.some(option => option.value === lightControl));
    assert.equal(nodes.get('home-cards-lighting-test-name').value, 'Nappali világítás');
    console.log('PASS: lighting block auto-discovery, no per-circuit/state mapping, save and retained filtered selection');
    /* Az IRC saját ikonja importból és rejtett oldalról sem cserélhető le.
     * A ténylegesen csatolt DOM-ot vizsgáljuk, nem a stub régi ID-bejegyzéseit. */
    const attached = (id, parent = nodes.get('home-cards')) => parent.id === id ? parent :
        parent.children.map(child => attached(id, child)).find(Boolean);
    const ircControl = '20000000-0000-0000-0000000000000001';
    const ircSecond = '20000000-0000-0000-0000000000000002';
    const ircEntry = {uuid:'20000000-0000-0000-0000000000000003', control_uuid:ircControl,
        action_uuid:ircControl, name:'Nappali IRC', type:'IRoomControllerV2', secured:false,
        room:'Nappali', category:'Klíma', irc:'{}', irc_blocked:false};
    const ircCatalog = [...lightCatalog, ircEntry,
        {...ircEntry, uuid:'20000000-0000-0000-0000000000000004'},
        {...ircEntry, control_uuid:ircSecond, name:'Háló IRC', room:'Háló'},
        {...ircEntry, control_uuid:'30000000-0000-0000-0000000000000001', type:'IRoomController'},
        {...ircEntry, control_uuid:'30000000-0000-0000-0000000000000002', irc:''},
        {...ircEntry, control_uuid:'30000000-0000-0000-0000000000000003', action_uuid:''}];
    state = {schema:2, consumers:[], resources_installed:false, pages:[
        {id:'irc-page', title:'Földszint', cards:[{id:'irc-test', name:'Nappali termosztát', icon:'lamp',
            type:'irc', control_uuid:ircControl, source:{uuid:'invalid', scale:0, offset:null}, unit:'bad', decimals:99}]},
        {id:'irc-hidden', title:'Emelet', cards:[
            {id:'irc-no-icon', name:'Háló termosztát', type:'irc', control_uuid:ircSecond},
            {id:'irc-bad-icon', name:'Importált termosztát', icon:'<img onerror=alert(1)>', type:'irc', control_uuid:ircControl}]}],
        intercom:{}, revision:0, busy:false, status:''};
    const ircContext = {...sandbox}; vm.createContext(ircContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',ircContext);
    await ircContext.home.refresh(ircCatalog);
    edit('home-room', '', 'onchange');
    assert(nodes.get('home-cards-irc-test-type').children.some(option => option.value === 'irc'));
    const ircOptions = nodes.get('home-cards-irc-test-control_uuid').children.map(option => option.value);
    assert.deepEqual(ircOptions, ['', ircControl, ircSecond]);
    for (const field of ['icon', 'uuid', 'scale', 'offset', 'unit', 'decimals', 'warning_low', 'warning_high', 'warning_color'])
        assert.equal(attached('home-cards-irc-test-' + field), undefined, field);
    assert.equal(attached('home-cards-irc-test-icon-preview'), undefined);
    const missingIrcPost = posts.length;await click('home-save');assert.equal(posts.length,missingIrcPost+1);
    state.resources_installed=true;state.resources_version=3;await ircContext.home.refresh(ircCatalog);
    assert.equal(attached('home-cards-irc-test-icon-preview').hidden,true);
    state.resources_version=4;await ircContext.home.refresh(ircCatalog);
    const ircPreview = attached('home-cards-irc-test-icon-preview');
    assert(ircPreview);
    assert.equal(ircPreview.hidden, false);
    assert.equal(ircPreview.parentNode.hidden, false);
    assert.equal(ircPreview.src, '/api/templates/home-icon?id=thermostat-irc&v=4');
    assert.equal(ircPreview.alt, 'IRC termosztát előnézete');
    const ircPosts = posts.length;
    await click('home-save');
    assert.equal(posts.length, ircPosts + 1);
    const storedIrc = posts.at(-1).pages.flatMap(page => page.cards);
    for (const card of storedIrc) {
        assert.equal(card.icon, 'thermostat-irc');
        assert.deepEqual(Object.keys(card).sort(), ['control_uuid', 'icon', 'id', 'name', 'type']);
    }
    /* Visszaváltáskor a korábbi általános ikon/mezők maradnak meg;
     * a más típusú blokk viszont nem kerülhet automatikusan az IRC céljába. */
    edit('home-cards-irc-test-type', 'value', 'onchange');
    assert.equal(attached('home-cards-irc-test-icon').value, 'lamp');
    assert(attached('home-cards-irc-test-uuid'));
    assert(attached('home-cards-irc-test-decimals'));
    edit('home-cards-irc-test-icon', 'washing-machine', 'onchange');
    edit('home-cards-irc-test-type', 'switch', 'onchange');
    edit('home-cards-irc-test-control_uuid', control, 'onchange');
    edit('home-cards-irc-test-type', 'irc', 'onchange');
    assert.equal(nodes.get('home-cards-irc-test-control_uuid').value, '');
    assert.equal(attached('home-cards-irc-test-icon'), undefined);
    assert.equal(attached('home-cards-irc-test-icon-preview').src, '/api/templates/home-icon?id=thermostat-irc&v=4');
    await click('home-save');
    assert.equal(posts.length, ircPosts + 1);
    edit('home-cards-irc-test-control_uuid', ircControl, 'onchange');
    await click('home-save');
    assert.equal(posts.length, ircPosts + 2);
    assert.equal(posts.at(-1).pages[0].cards[0].icon, 'thermostat-irc');
    edit('home-cards-irc-test-type', 'lighting', 'onchange');
    assert.equal(attached('home-cards-irc-test-icon').value, 'washing-machine');
    assert.equal(nodes.get('home-cards-irc-test-control_uuid').value, '');
    assert(nodes.get('home-cards-irc-test-control_uuid').children.some(option => option.value === lightControl));
    assert(!nodes.get('home-cards-irc-test-control_uuid').children.some(option => option.value === ircControl));
    click('home-page-tab-irc-hidden');
    edit('home-cards-irc-no-icon-type', 'value', 'onchange');
    assert.equal(attached('home-cards-irc-no-icon-icon').value, 'thermostat');
    console.log('PASS: IRC parent discovery, fixed icon from SD and missing/outdated pack fallback, imported/hidden-page normalization, minimal save, generic type restoration and incompatible-target reset');
    /* Csak a C katalógus által támogatottnak jelölt automatikus és központi
     * árnyékolás választható; a kézi Jalousie nem kap shading-metaadatot. */
    const blindControl = '40000000-0000-0000-0000000000000001';
    const centralControl = '40000000-0000-0000-0000000000000002';
    const blockedControl = '40000000-0000-0000-0000000000000003';
    const blindEntry = {uuid:'40000000-0000-0000-0000000000000004', control_uuid:blindControl,
        action_uuid:blindControl, name:'Nappali redőny', type:'Jalousie', secured:false,
        room:'Nappali', category:'Árnyékolás', shading:'{}', shading_blocked:false, shading_central:false};
    const shadingCatalog = [...ircCatalog, blindEntry,
        {...blindEntry, uuid:'40000000-0000-0000-0000000000000005'},
        {...blindEntry, control_uuid:centralControl, name:'Emeleti központi árnyékolás', room:'Emelet',
            type:'CentralJalousie', shading_central:true},
        {...blindEntry, control_uuid:blockedControl, name:'Csak olvasható redőny', secured:true, shading_blocked:true},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000001', name:'Kézi redőny', shading:undefined},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000002', shading:''},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000003', shading:{}},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000004', action_uuid:''},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000005', type:'Switch'},
        {...blindEntry, control_uuid:'50000000-0000-0000-0000000000000006', type:'CentralJalousie', shading:undefined}];
    state = {schema:2, consumers:[], resources_installed:false, pages:[
        {id:'shading-page', title:'Földszint', cards:[{id:'shading-test', name:'Nappali redőny', icon:'lamp',
            type:'shading', control_uuid:blindControl, source:{uuid:'invalid', scale:0, offset:null},
            unit:'bad', decimals:99, warning_low:'invalid', warning_high:{}, warning_color:'bad'}]},
        {id:'shading-hidden', title:'Emelet', cards:[
            {id:'shading-no-icon', name:'Központi árnyékolás', type:'shading', control_uuid:centralControl},
            {id:'shading-bad-icon', name:'Importált redőny', icon:'<img onerror=alert(1)>', type:'shading',
                control_uuid:blindControl, source:{uuid:'invalid', scale:0}}]}],
        intercom:{}, revision:0, busy:false, status:''};
    const shadingContext = {...sandbox}; vm.createContext(shadingContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',shadingContext);
    await shadingContext.home.refresh(shadingCatalog);
    edit('home-room', '', 'onchange');
    edit('home-category', '', 'onchange');
    const shadingPrefix = 'home-cards-shading-test-';
    assert(nodes.get(shadingPrefix + 'type').children.some(option => option.value === 'shading'));
    assert.deepEqual(nodes.get(shadingPrefix + 'control_uuid').children.map(option => option.value),
        ['', blindControl, centralControl, blockedControl]);
    for (const field of ['icon', 'uuid', 'scale', 'offset', 'unit', 'decimals', 'warning_low', 'warning_high', 'warning_color'])
        assert.equal(attached(shadingPrefix + field), undefined, field);
    assert.equal(attached(shadingPrefix + 'icon-preview'), undefined);
    state.resources_installed=true;state.resources_version=4;await shadingContext.home.refresh(shadingCatalog);
    const shadingPreview = attached(shadingPrefix + 'icon-preview');
    assert.equal(shadingPreview.hidden, false);
    assert.equal(shadingPreview.parentNode.hidden, false);
    assert.equal(shadingPreview.src, '/api/templates/home-icon?id=blinds-shading&v=4');
    assert.equal(shadingPreview.alt, 'Redőnyvezérlő előnézete');
    const shadingPosts = posts.length;
    await click('home-save');
    assert.equal(posts.length, shadingPosts + 1);
    for (const card of posts.at(-1).pages.flatMap(page => page.cards)) {
        assert.equal(card.icon, 'blinds-shading');
        assert.equal(card.type, 'shading');
        assert.deepEqual(Object.keys(card).sort(), ['control_uuid', 'icon', 'id', 'name', 'type']);
    }
    /* A szűrés nem törölheti a már kiválasztott másik helyiséget. */
    edit('home-room', JSON.stringify('Emelet'), 'onchange');
    assert.deepEqual(nodes.get(shadingPrefix + 'control_uuid').children.map(option => option.value),
        ['', centralControl, blindControl]);
    assert.equal(nodes.get(shadingPrefix + 'control_uuid').value, blindControl);
    edit(shadingPrefix + 'control_uuid', '', 'onchange');
    edit('home-category', JSON.stringify('Világítás'), 'onchange');
    assert.deepEqual(nodes.get(shadingPrefix + 'control_uuid').children.map(option => option.value), ['']);
    edit('home-room', '', 'onchange');
    edit('home-category', '', 'onchange');
    await click('home-save');
    assert.equal(posts.length, shadingPosts + 1);
    assert.equal(nodes.get('home-status').className, 'feedback-error');
    /* A két kötött típus közötti váltás is megőrzi az általános ikont,
     * miközben az össze nem illő szülőblokkot törli. */
    edit(shadingPrefix + 'control_uuid', blindControl, 'onchange');
    edit(shadingPrefix + 'type', 'irc', 'onchange');
    assert.equal(nodes.get(shadingPrefix + 'control_uuid').value, '');
    assert.equal(attached(shadingPrefix + 'icon-preview').src, '/api/templates/home-icon?id=thermostat-irc&v=4');
    edit(shadingPrefix + 'control_uuid', ircControl, 'onchange');
    edit(shadingPrefix + 'type', 'shading', 'onchange');
    assert.equal(nodes.get(shadingPrefix + 'control_uuid').value, '');
    assert.equal(attached(shadingPrefix + 'icon'), undefined);
    edit(shadingPrefix + 'control_uuid', centralControl, 'onchange');
    await click('home-save');
    assert.equal(posts.length, shadingPosts + 2);
    assert.equal(posts.at(-1).pages[0].cards[0].control_uuid, centralControl);
    edit(shadingPrefix + 'type', 'value', 'onchange');
    assert.equal(attached(shadingPrefix + 'icon').value, 'lamp');
    assert(attached(shadingPrefix + 'uuid'));
    edit(shadingPrefix + 'icon', 'washing-machine', 'onchange');
    edit(shadingPrefix + 'type', 'switch', 'onchange');
    edit(shadingPrefix + 'control_uuid', control, 'onchange');
    edit(shadingPrefix + 'type', 'shading', 'onchange');
    assert.equal(nodes.get(shadingPrefix + 'control_uuid').value, '');
    edit(shadingPrefix + 'type', 'lighting', 'onchange');
    assert.equal(attached(shadingPrefix + 'icon').value, 'washing-machine');
    assert.equal(nodes.get(shadingPrefix + 'control_uuid').value, '');
    assert(nodes.get(shadingPrefix + 'control_uuid').children.some(option => option.value === lightControl));
    assert(!nodes.get(shadingPrefix + 'control_uuid').children.some(option => option.value === blindControl));
    click('home-page-tab-shading-hidden');
    edit('home-cards-shading-no-icon-type', 'value', 'onchange');
    assert.equal(attached('home-cards-shading-no-icon-icon').value, 'blinds');
    console.log('PASS: automatic/central shading metadata filter, fixed SD icon, imported/hidden-page normalization, minimal save, room/category filters and fixed/generic type changes');
    /* Kártyasorrend: azonos oldalon objektumot mozgatunk, minden mezővel együtt. */
    state = {schema:2, consumers:[], resources_installed:true, resources_version:3, intercom:{}, revision:0, busy:false,
        pages:[{id:'order',title:'Rendezhető',cards:[
            {id:'first',name:'Első',type:'value',icon:'lamp',source:{uuid,scale:2,offset:1},unit:'kW',decimals:2},
            {id:'second',name:'Második',type:'pulse',icon:'door',control_uuid:control},
            {id:'third',name:'Harmadik',type:'pulse',icon:'gate',control_uuid:control}]},
            {id:'unchanged',title:'Másik oldal',cards:[{id:'other',name:'Marad',type:'pulse',icon:'door',control_uuid:control}]}]};
    const orderContext = {...sandbox}; vm.createContext(orderContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',orderContext);
    await orderContext.home.refresh(catalog);
    const order = () => nodes.get('home-cards').children.map(box=>box.id);
    assert.deepEqual(order(),['first','second','third'].map(id=>'home-cards-'+id+'-editor'));
    assert(nodes.get('home-cards-first-move-up').disabled);
    assert(nodes.get('home-cards-third-move-down').disabled);
    const typeGrid=nodes.get('home-cards').children[0].children[1];
    assert.equal(typeGrid.children[0]['data-field'],'type');
    const firstGrid=nodes.get('home-cards').children[0].children[2].children[0];
    assert.deepEqual(firstGrid.children.slice(0,2).map(label=>label['data-field']),['uuid','name']);
    edit('home-cards-first-name','Szerkesztett, még nem mentett');
    edit('home-cards-first-scale','3.5');
    edit('home-cards-first-offset','-4');
    const nameInput=nodes.get('home-cards-first-name');nameInput.focus();nameInput.setSelectionRange(2,8);
    edit('home-room','','onchange');
    assert.equal(sandbox.document.activeElement.id,'home-cards-first-name');
    assert.equal(sandbox.document.activeElement.selectionStart,2);
    assert.equal(sandbox.document.activeElement.selectionEnd,8);
    nodes.get('home-cards-first-move-down').focus();click('home-cards-first-move-down');
    assert.deepEqual(order(),['second','first','third'].map(id=>'home-cards-'+id+'-editor'));
    assert.equal(sandbox.document.activeElement.id,'home-cards-first-move-down');
    assert.equal(nodes.get('home-cards-first-name').value,'Szerkesztett, még nem mentett');
    assert.equal(nodes.get('home-cards-first-scale').value,'3.5');
    click('home-cards-first-move-down');
    assert.deepEqual(order(),['second','third','first'].map(id=>'home-cards-'+id+'-editor'));
    assert.equal(nodes.get('home-cards-first-move-down').disabled,true);
    assert.equal(sandbox.document.activeElement.id,'home-cards-first-move-up');
    click('home-cards-first-move-down'); /* A tiltott határ programból sem léphető át. */
    assert.deepEqual(order(),['second','third','first'].map(id=>'home-cards-'+id+'-editor'));
    click('home-cards-first-move-up');
    const orderPosts=posts.length;await click('home-save');
    assert.equal(posts.length,orderPosts+1);
    assert.deepEqual(posts.at(-1).pages[0].cards.map(card=>card.id),['second','first','third']);
    const savedFirst=posts.at(-1).pages[0].cards[1];
    assert.equal(savedFirst.name,'Szerkesztett, még nem mentett');
    assert.deepEqual(savedFirst.source,{uuid,scale:3.5,offset:-4});
    assert.equal(savedFirst.unit,'kW');assert.equal(savedFirst.decimals,2);
    assert.equal(posts.at(-1).pages[1].cards[0].id,'other');
    const reloadContext={...sandbox};vm.createContext(reloadContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',reloadContext);
    await reloadContext.home.refresh(catalog);
    assert.deepEqual(order(),['second','first','third'].map(id=>'home-cards-'+id+'-editor'));
    click('home-page-tab-unchanged');
    assert.deepEqual(order(),['home-cards-other-editor']);
    assert(nodes.get('home-cards-other-move-up').disabled);assert(nodes.get('home-cards-other-move-down').disabled);
    console.log('PASS: type-first empty card validation, DOM field order, per-page up/down boundaries, unsaved data and focus, serialized ordering and reload');
    /* A nyolc új kártya szülőblokkot használ; az állapotmezőket a katalógus oldja fel. */
    const blockTypes = [['dimmer','Dimmer'],['central-lighting','CentralLightController'],['slider','Slider'],
        ['timed-switch','TimedSwitch'],['window-monitor','WindowMonitor'],['gate','Gate'],['window','Window'],['audio','AudioZoneV2']];
    const blockId = index => '60000000-0000-0000-' + String(index).padStart(16,'0');
    const advancedCatalog = blockTypes.map(([controls_type,type],index)=>({
        uuid:blockId(index+100),control_uuid:blockId(index+1),action_uuid:controls_type==='window-monitor' ? '' : blockId(index+50),
        name:'Blokk '+controls_type,room:'Nappali',category:'Új blokkok',type,controls_type,controls:'{}',controls_blocked:false,
        gate_animation:controls_type==='gate' ? 3 : undefined}));
    advancedCatalog.push({...advancedCatalog[0],uuid:blockId(200)});
    for(const change of [{controls:undefined},{controls:''},{controls:{}},{controls_type:'slider'},{type:'Switch'},{action_uuid:''}])
        advancedCatalog.push({...advancedCatalog[0],control_uuid:blockId(advancedCatalog.length+300),...change});
    state={schema:2,consumers:[],resources_installed:true,resources_version:4,intercom:{},revision:0,busy:false,pages:[{
        id:'advanced',title:'Új funkciók',cards:blockTypes.map(([type],index)=>({
            id:'block-'+type,name:'Kártya '+type,type,icon:'lamp',control_uuid:blockId(index+1),
            source:{uuid:'bad',scale:0,offset:null},unit:'bad',decimals:99}))}]};
    const advancedContext={...sandbox};vm.createContext(advancedContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',advancedContext);
    await advancedContext.home.refresh(advancedCatalog);
    const expectedIcons={'central-lighting':'central-lighting','window-monitor':'window-monitor',gate:'gate-double',window:'motor-window',audio:'audio-player'};
    for(const [index,[type]] of blockTypes.entries()) {
        const prefix='home-cards-block-'+type+'-';
        assert.deepEqual(nodes.get(prefix+'control_uuid').children.map(option=>option.value),['',blockId(index+1)]);
        for(const key of ['uuid','scale','offset','unit','decimals','warning_low','warning_high','warning_color'])
            assert.equal(attached(prefix+key),undefined,type+': '+key);
        if(expectedIcons[type]) {
            assert.equal(attached(prefix+'icon'),undefined);
            assert.equal(attached(prefix+'icon-preview').src,'/api/templates/home-icon?id='+expectedIcons[type]+'&v=4');
        } else assert.equal(attached(prefix+'icon').value,'lamp');
    }
    assert.equal(nodes.get('home-cards-block-timed-switch-allow_permanent').checked,true);
    assert.match(nodes.get('home-cards-block-window-monitor-control_uuid').children[1].textContent,/összesítő/);
    const advancedPosts=posts.length;await click('home-save');assert.equal(posts.length,advancedPosts+1);
    for(const card of posts.at(-1).pages[0].cards) {
        assert.deepEqual(Object.keys(card).sort(),['control_uuid','icon','id','name','type',...(card.type==='timed-switch' ? ['allow_permanent'] : [])].sort());
        assert.equal(card.icon,expectedIcons[card.type] || 'lamp');
    }
    const permanent=nodes.get('home-cards-block-timed-switch-allow_permanent');permanent.checked=false;permanent.onchange();
    await click('home-save');assert.equal(posts.at(-1).pages[0].cards.find(card=>card.type==='timed-switch').allow_permanent,false);
    /* A kapu szigorúan numerikus 0..5 animációs típusból kap képet. */
    const gateMap=['gate-garage','gate-left','gate-right','gate-double','gate-slide-left','gate-slide-right'];
    const gateNames=['Garázskapu','Balra nyíló kapu','Jobbra nyíló kapu','Dupla kapu','Balra nyíló tolóajtó','Jobbra nyíló tolóajtó'];
    const gateEntry=advancedCatalog.find(item=>item.controls_type==='gate');
    for(let index=0;index<6;index++) {
        gateEntry.gate_animation=index;await advancedContext.home.refresh(advancedCatalog);
        assert.equal(attached('home-cards-block-gate-icon-preview').src,'/api/templates/home-icon?id='+gateMap[index]+'&v=4');
        assert.equal(attached('home-cards-block-gate-gate-type').textContent,'Felismert kaputípus: '+gateNames[index]);
    }
    for(const invalid of ['2',6,-1,0.5,undefined]) {
        gateEntry.gate_animation=invalid;await advancedContext.home.refresh(advancedCatalog);
        assert.equal(attached('home-cards-block-gate-icon-preview').src,'/api/templates/home-icon?id=gate&v=4');
    }
    /* A listanévnek látszó HTML szöveg marad, és az egyedi cím megmarad párosításkor. */
    advancedCatalog.push({...gateEntry,control_uuid:blockId(999),name:'<img src=x onerror=alert(1)> kapu',gate_animation:0});
    await advancedContext.home.refresh(advancedCatalog);
    edit('home-cards-block-gate-name','Saját kapunév');
    edit('home-cards-block-gate-control_uuid',blockId(999),'onchange');
    assert.equal(nodes.get('home-cards-block-gate-name').value,'Saját kapunév');
    assert.equal(attached('home-cards-block-gate-icon-preview').src,'/api/templates/home-icon?id=gate-garage&v=4');
    /* Új típushoz funkcióikon jár; választható képből kötöttbe és vissza sem vész el a választás. */
    click('home-add-card');
    for(const type of ['dimmer','slider','timed-switch']) {
        edit('home-cards-item-1-type','','onchange');edit('home-cards-item-1-type',type,'onchange');
        assert.equal(nodes.get('home-cards-item-1-icon').value,type);
    }
    edit('home-cards-item-1-icon','washing-machine','onchange');
    edit('home-cards-item-1-type','gate','onchange');
    edit('home-cards-item-1-type','slider','onchange');
    assert.equal(nodes.get('home-cards-item-1-icon').value,'washing-machine');
    /* Általános kártyán egy új, funkcionális SD-ikon is szabadon használható. */
    edit('home-cards-item-1-type','value','onchange');edit('home-cards-item-1-icon','gate-double','onchange');
    edit('home-room','','onchange');assert.equal(nodes.get('home-cards-item-1-icon').value,'gate-double');
    click('home-cards-item-1-remove');
    console.log('PASS: eight typed block filters, read-only monitor, minimal save, optional permanent mode, six gate icons and unknown fallback, fixed/selectable SD resources');
    console.log('PASS: actual home editor, catalog icons, consumer transforms, explicit command target, pulse/switch, filters retaining mappings, pending/failure states, separate video credentials, retained blank password, no integration secret or background edit loss');
})().catch(error=>{console.error(error);process.exitCode=1;});
