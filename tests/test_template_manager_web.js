/* A valódi sablonkezelő törlési felülete lassú és hibás HTTP-kérés közben. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
class Element {
    constructor(tag) { this.tagName=tag.toUpperCase();this.children=[];this.attributes={};this.style={};this.textContent='';this.className='';this.disabled=false;this.hidden=false; }
    append(...children) { this.children.push(...children); }
    replaceChildren() { this.children=[]; }
    setAttribute(key,value) { this.attributes[key]=value; }
    /* Minimális táblázat-API a frissítési listához. */
    get tBodies() { return this.children.filter(node=>node.tagName==='TBODY'); }
    createTBody() { const body=new Element('tbody');this.children.push(body);return body; }
    insertRow() { const row=new Element('tr');this.children.push(row);return row; }
    insertCell() { const cell=new Element('td');this.children.push(cell);return cell; }
}
const nodes=new Map(['message','install-message','list','zip','check','install',
    'update-check','update-all','update-message','update-list'].map(id=>[id,new Element('div')]));
let templates=[{id:'builtin',name:'Rendszer'},{id:'test-theme',name:'Próba',version:1}];
let finish,posts=0;
/* A fejlesztői firmware-en a telepített témák újabbak lehetnek a kiadásnál. */
let updateState={state:'ready',message:'Minden telepített téma naprakész (v3.1.4).',items:[
    {id:'solar',name:'Nap pálya',installed:6,available:5,update:false,status:''},
    {id:'battery',name:'Akku',installed:2,available:3,update:true,status:''}]};
const sandbox={console,TextEncoder,TextDecoder,DataView,Uint8Array,Map,Set,ArrayBuffer,Blob,DecompressionStream,
    document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},
    Image:class extends Element{constructor(){super('img');}},confirm:()=>true,
    fetch:async(path,options={})=>{
        if(path.startsWith('/api/templates/updates'))return {ok:true,json:async()=>updateState};
        if(options.method==='POST'){
            if(path==='/api/templates/validate')return new Promise(resolve=>{finish=resolve;});
            posts++;assert.equal(path,'/api/templates/delete');assert.equal(options.body,'test-theme');
            return new Promise(resolve=>{finish=resolve;});
        }
        return {ok:true,json:async()=>({active:'builtin',templates,resources:[{id:'loxone-home',name:'Otthon',version:3}]})};
    }};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('assets/web/template-manager.js','utf8'),sandbox);
const settled=()=>new Promise(resolve=>setImmediate(resolve));
function removeButton(){
    const article=nodes.get('list').children.at(-1);
    const actions=article.children[1].children[2];
    return actions.children.find(node=>node.textContent==='Törlés');
}
(async()=>{
    await settled();
    const resourcePreview=nodes.get('list').children[0].children[0].children[0];
    assert(resourcePreview.src.includes('id=loxone-home'));
    removeButton().onclick();await settled();
    assert.equal(posts,1);assert.equal(nodes.get('message').className,'working');
    assert(nodes.get('message').textContent.includes('Törlés folyamatban'));
    assert.equal(nodes.get('list').inert,true);assert.equal(nodes.get('list').attributes['aria-busy'],'true');
    removeButton().onclick();assert.equal(posts,1);
    finish({ok:false,text:async()=>'Az SD-kártya nem írható.'});await settled();
    assert.equal(nodes.get('list').inert,false);assert.equal(nodes.get('message').className,'');
    assert(nodes.get('message').textContent.includes('nem írható'));assert.equal(nodes.get('list').children.length,3);
    removeButton().onclick();await settled();assert.equal(posts,2);
    templates=templates.slice(0,1);finish({ok:true,text:async()=>'Sikeres'});await settled();
    assert.equal(nodes.get('list').children.length,2);assert.equal(nodes.get('list').inert,false);
    /* A ZIP-telepítés sosem a lista fölötti sávba ír. */
    assert.equal(nodes.get('install-message').textContent,'');
    /* Elrendezés: a telepítés visszajelzése a gombok alatt, a GitHubos
     * frissítésé a saját szakaszában, a listaműveleteké a lista fölött. */
    const page=fs.readFileSync('assets/web/template-manager.html','utf8');
    const at=needle=>{const index=page.indexOf(needle);assert(index>=0,needle);return index;};
    assert(at('id="install"')<at('id="install-message"'));
    assert(at('id="install-message"')<at('id="updates"'));
    assert(at('id="updates"')<at('id="update-message"'));
    assert(at('id="update-message"')<at('id="message"'));
    assert(at('id="message"')<at('id="list"'));
    /* Az előnézetek betöltése alatt a telepítés és a frissítéskeresés zárva. */
    assert.equal(nodes.get('update-check').disabled,true);
    assert.equal(nodes.get('zip').disabled,true);
    const thumbs=[];
    for(const article of nodes.get('list').children)
        for(const image of article.children[0].children)thumbs.push(image);
    assert(thumbs.length>=2);
    thumbs.forEach(image=>image.onload());
    assert.equal(nodes.get('update-check').disabled,false);
    assert.equal(nodes.get('zip').disabled,false);
    /* A GitHubos frissítés szakasza: a „Helyi újabb” állapot és a rejtett gomb. */
    await nodes.get('update-check').onclick();await settled();
    const rows=nodes.get('update-list').children[0].children;
    /* Új keresés előtt a korábbi lista eltűnik, nem marad félkész találat. */
    assert(nodes.get('update-list').children[0].children.length>0);
    assert.equal(rows[0].children[3].textContent,'Helyi újabb');
    assert.equal(nodes.get('update-all').hidden,false);
    assert.equal(nodes.get('update-all').textContent,'Összes frissítése (1)');
    updateState={state:'ready',message:'Minden telepített téma naprakész (v3.1.4).',
        items:[{id:'solar',name:'Nap pálya',installed:6,available:5,update:false,status:''}]};
    await nodes.get('update-check').onclick();await settled();
    assert.equal(nodes.get('update-all').hidden,true);
    /* A hidden attribútum erősebb a gombok display szabályánál. */
    assert(fs.readFileSync('assets/web/common.css','utf8').includes('[hidden]{display:none!important}'));
    console.log('PASS: stored common-resource thumbnail, separate install/list status bars, immediate delete spinner/announcement, locked duplicate actions, failure recovery and successful refresh, locally newer themes, controls locked while previews load and a truly hidden bulk-update button');
})().catch(error=>{console.error(error);process.exitCode=1;});
