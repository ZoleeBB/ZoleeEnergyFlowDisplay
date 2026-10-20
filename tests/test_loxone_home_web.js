/* A kiadott szerkesztő valódi JS-e: szerkesztés, szűrés, mentés és hibák. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
const nodes = new Map();
class Element {
    constructor(tag) { this.tagName=tag.toUpperCase();this.children=[];this._value='';this.checked=false;this.disabled=false;this.textContent=''; }
    set id(value) { this._id=value;nodes.set(value,this); } get id() { return this._id; }
    set value(value) { this._value=String(value); } get value() { return this._value; }
    append(child) { this.children.push(child);if(this.tagName==='SELECT'&&this.children.length===1)this.value=child.value; }
    replaceChildren() { this.children=[];this.value=''; }
}
for(const id of ['home-editor','home-status','home-save','home-add-consumer','home-add-card','home-consumers','home-cards','home-room','home-category']) {
    const node=new Element(id.includes('room')||id.includes('category')?'select':'div');node.id=id;
}
const icons=JSON.parse(fs.readFileSync('assets/loxone-home/catalog.json')).icons.map(({id,hu,en})=>({id,hu,en}));
const uuid='11223344-5566-7788-99aabbccddeeff00',control='aabbccdd-5566-7788-99aabbccddeeff00';
const catalog=[{uuid,control_uuid:control,name:'<img src=x> Hőmérő',room:'Nappali',category:'Klíma',state:'value'},
    {uuid:control,name:'Csak állapot',room:'Kert',category:'Kert',state:'value'}];
let state={schema:1,consumers:[],cards:[],busy:false,revision:0,status:''}, reject=false, offline=false, hold=false;
const posts=[];
const sandbox={console,TextEncoder,document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},apiFetch:async(url,options={})=>{
    assert.equal(url,'/api/loxone/home');
    if(offline)throw Error('kapcsolat megszakadt');
    if(options.method==='POST') {
        if(reject)return {ok:false,text:async()=>'Az SD-kártya nem írható.'};
        assert.equal(options.headers['X-Zolee-Loxone'],'1');posts.push(JSON.parse(options.body));
        state.busy=hold;
        if(!hold){state={...state,...JSON.parse(options.body),revision:state.revision+1,status:'Otthon-beállítások elmentve.'};}
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
    click('home-add-consumer');assert.equal(nodes.get('home-consumers-item-1-icon').children.length,64);
    await click('home-save');assert.equal(posts.length,0);assert(nodes.get('home-status').textContent.includes('nevet'));
    edit('home-consumers-item-1-name','Mosógép');edit('home-consumers-item-1-uuid',uuid,'onchange');edit('home-consumers-item-1-scale','.001');
    await click('home-save');assert.equal(posts.length,1);
    assert.equal(posts[0].consumers[0].source.scale,.001);assert.equal(posts[0].consumers[0].average_kw,null);
    assert.equal(posts[0].consumers[0].suggest_start,false);assert(!('password' in posts[0]));
    edit('home-consumers-item-1-name','Még nincs mentve');
    await sandbox.home.refresh(catalog);assert.equal(nodes.get('home-consumers-item-1-name').value,'Még nincs mentve');
    edit('home-room',JSON.stringify('Kert'),'onchange');
    assert(nodes.get('home-consumers-item-1-uuid').children.some(node=>node.value===uuid));
    assert.equal(nodes.get('home-consumers-item-1-name').value,'Még nincs mentve');
    edit('home-consumers-item-1-average_kw','-1');await click('home-save');assert.equal(posts.length,1);
    edit('home-consumers-item-1-average_kw','2.1');const suggest=nodes.get('home-consumers-item-1-suggest_start');suggest.checked=true;suggest.onchange();
    click('home-add-card');
    edit('home-cards-item-1-type','pulse','onchange');
    assert(nodes.get('home-consumers-item-1-name') !== nodes.get('home-cards-item-1-name'));
    /* A két oldal azonos elemkulcsai külön DOM-névtérben élnek. */
    const target=nodes.get('home-cards-item-1-control_uuid');
    assert(!target.children.some(node=>node.value===uuid));
    edit('home-room','','onchange');
    assert(nodes.get('home-cards-item-1-control_uuid').children.some(node=>node.value===control));
    edit('home-cards-item-1-name','Kapunyitás');edit('home-cards-item-1-control_uuid',control,'onchange');
    await click('home-save');assert.equal(posts.length,2);assert.equal(posts[1].cards[0].type,'pulse');
    assert.equal(posts[1].cards[0].control_uuid,control);assert.equal(posts[1].consumers[0].name,'Még nincs mentve');
    edit('home-cards-item-1-type','switch','onchange');edit('home-cards-item-1-uuid',uuid,'onchange');
    assert(nodes.get('home-cards-item-1-scale').disabled);
    await click('home-save');assert.equal(posts.length,3);assert.equal(posts[2].cards[0].source.scale,1);
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
    assert.equal(posts.at(-1).cards[0].warning_low,null);assert.equal(posts.at(-1).cards[0].decimals,1);
    for(let i=0;i<13;i++)click('home-add-consumer');
    assert.equal(nodes.get('home-consumers').children.length,12);assert(nodes.get('home-add-consumer').disabled);
    const firstBox=nodes.get('home-consumers').children[0];firstBox.children.at(-1).onclick();
    assert.equal(nodes.get('home-consumers').children.length,11);assert(!nodes.get('home-add-consumer').disabled);
    console.log('PASS: actual home editor, 64 icons, consumer transforms, explicit command target, pulse/switch, filters retaining mappings, pending/failure states, no secret submission or background edit loss');
})().catch(error=>{console.error(error);process.exitCode=1;});
