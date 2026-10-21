/* A valódi sablonkezelő törlési felülete lassú és hibás HTTP-kérés közben. */
const fs = require('fs'), vm = require('vm'), assert = require('node:assert/strict');
class Element {
    constructor(tag) { this.tagName=tag.toUpperCase();this.children=[];this.attributes={};this.style={};this.textContent='';this.className='';this.disabled=false; }
    append(...children) { this.children.push(...children); }
    replaceChildren() { this.children=[]; }
    setAttribute(key,value) { this.attributes[key]=value; }
}
const nodes=new Map(['message','list','zip','check','install'].map(id=>[id,new Element('div')]));
let templates=[{id:'builtin',name:'Rendszer'},{id:'test-theme',name:'Próba',version:1}];
let finish,posts=0;
const sandbox={console,TextEncoder,TextDecoder,DataView,Uint8Array,Map,Set,ArrayBuffer,Blob,DecompressionStream,
    document:{getElementById:id=>nodes.get(id),createElement:tag=>new Element(tag)},
    Image:class extends Element{constructor(){super('img');}},confirm:()=>true,
    fetch:async(path,options={})=>{
        if(options.method==='POST'){
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
    console.log('PASS: stored common-resource thumbnail, immediate delete spinner/announcement, locked duplicate actions, failure recovery and successful refresh');
})().catch(error=>{console.error(error);process.exitCode=1;});
