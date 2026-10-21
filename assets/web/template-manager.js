'use strict';
const ZIP_LIMIT=8388608;
function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
function safeAsset(name){return /^[a-z0-9_-]+\.png$/.test(name)&&name.length<=64;}
async function unpackZip(buffer){
 if(buffer.byteLength<22||buffer.byteLength>ZIP_LIMIT)throw Error('Hibás ZIP-méret');
 const v=new DataView(buffer),bytes=new Uint8Array(buffer),u16=p=>v.getUint16(p,true),u32=p=>v.getUint32(p,true);
 let end=-1;for(let p=bytes.length-22;p>=Math.max(0,bytes.length-65557);p--)if(u32(p)===0x06054b50&&p+22+u16(p+20)===bytes.length){end=p;break;}
 if(end<0)throw Error('Hibás ZIP');
 const count=u16(end+10),offset=u32(end+16),cdSize=u32(end+12);
 if(u16(end+4)||u16(end+6)||u16(end+8)!==count||!count||count>72||offset+cdSize!==end)throw Error('Nem támogatott ZIP');
 const files=new Map();let p=offset,total=0;
 for(let i=0;i<count;i++){
  if(p+46>end||u32(p)!==0x02014b50)throw Error('Hibás ZIP-könyvtár');
  const flags=u16(p+8),method=u16(p+10),crc=u32(p+16),compressed=u32(p+20),size=u32(p+24),n=u16(p+28),local=u32(p+42),mode=(u32(p+38)>>>16)&0xf000;
  if((flags&~(0x800|8|6))||(method!==0&&method!==8)||u16(p+34)||(mode&&mode!==0x8000)||!n||n>64||!size||size>2621440||(total+=size)>ZIP_LIMIT||p+46+n>end)throw Error('ZIP méretkorlát');
  const name=new TextDecoder('utf-8',{fatal:true}).decode(bytes.subarray(p+46,p+46+n));
  if((name!=='template.json'&&!safeAsset(name))||files.has(name))throw Error('Hibás ZIP útvonal vagy duplikáció');
  if(local+30>offset||u32(local)!==0x04034b50||u16(local+6)!==flags||u16(local+8)!==method||u16(local+26)!==n)throw Error('Hibás ZIP-fejléc');
  const start=local+30+n+u16(local+28);
  if(start+compressed>offset||new TextDecoder().decode(bytes.subarray(local+30,local+30+n))!==name)throw Error('Hibás ZIP-eltolás');
  let data=bytes.slice(start,start+compressed);
  if(method===8){
   let stream;try{stream=new DecompressionStream('deflate-raw');}catch(e){throw Error('A böngésző nem támogatja a ZIP előnézetet');}
   const reader=new Blob([data]).stream().pipeThrough(stream).getReader(),chunks=[];let length=0;
   try{while(true){const next=await reader.read();if(next.done)break;length+=next.value.length;if(length>size)throw Error('Hibás kicsomagolt ZIP-méret');chunks.push(next.value);}}catch(e){await reader.cancel().catch(()=>{});throw e;}
   data=new Uint8Array(length);let cursor=0;for(const chunk of chunks){data.set(chunk,cursor);cursor+=chunk.length;}
  }
  if(data.length!==size||crc32(data)!==crc)throw Error('ZIP CRC / méret');
  files.set(name,data);p+=46+n+u16(p+30)+u16(p+32);
 }
 if(p!==end||!files.has('template.json'))throw Error('Hiányzó template.json');
 if(count>34){const doc=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(files.get('template.json')));if(doc.kind!=='loxone-home-resources'||doc.id!=='loxone-home')throw Error('Túl sok fájl normál témához');}
 return files;
}
function pngInfo(bytes){
 const signature=[137,80,78,71,13,10,26,10];if(bytes.length<45||signature.some((x,i)=>bytes[i]!==x))throw Error('Hibás PNG-aláírás');
 const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),w=v.getUint32(16),h=v.getUint32(20);
 if(!w||!h||w>1280||h>800||bytes[24]!==8||![2,6].includes(bytes[25])||bytes[26]||bytes[27]||bytes[28])throw Error('Hibás PNG formátum');
 let p=8,ended=false;while(p+12<=bytes.length){let size=v.getUint32(p);if(p+12+size>bytes.length||crc32(bytes.subarray(p+4,p+8+size))!==v.getUint32(p+8+size))throw Error('Hibás PNG-ellenőrzőösszeg');
  let type=new TextDecoder().decode(bytes.subarray(p+4,p+8));if(p===8&&(type!=='IHDR'||size!==13))throw Error('Hibás PNG-fejléc');
  if(type==='IEND'){ended=size===0&&p+12===bytes.length;break;}p+=size+12;}
 if(!ended)throw Error('Hibás PNG-zárás');return {w,h,decoded:w*h*4};
}
function validateManifest(doc,files){
 if(!doc||doc.schema!==1||!Number.isInteger(doc.version)||doc.version<1||doc.version>65535||typeof doc.name!=='string'||new TextEncoder().encode(doc.name).length>80||!/^([a-z0-9_-]{1,32})$/.test(doc.id)||doc.id==='builtin')throw Error('Hibás sablonazonosító');
 const resources=doc.kind==='loxone-home-resources';
 if(resources){if(doc.id!=='loxone-home'||!Array.isArray(doc.icons)||doc.icons.length!==64||new Set(doc.icons.map(icon=>icon.id)).size!==64||doc.icons.some(icon=>!/^([a-z0-9_-]{1,32})$/.test(icon.id)||icon.file!=='icon-'+icon.id+'.png'||!files.has(icon.file)))throw Error('Hibás Loxone-erőforráskatalógus');}
 if(resources&&(doc.version>=2?(!doc.consumer_backgrounds||doc.consumer_backgrounds.light!=='consumers-light.png'||doc.consumer_backgrounds.dark!=='consumers-dark.png'):doc.consumer_backgrounds!==undefined))throw Error('Hiányzó fogyasztói háttérpár');
 else if(!resources&&(doc.kind!==undefined||doc.id==='loxone-home'))throw Error('Fenntartott erőforrásazonosító');
 const color=x=>/^#[0-9a-f]{6}$/i.test(x),types=['box','pulse','solar_disc','solar_arc','solar_timeline','label','value','image','weather','bar','gauge','flow','chart'];
 let encoded=0,decoded=0,assets=0;
 for(const [name,data] of files)if(name!=='template.json'){if(++assets>(resources?72:32))throw Error('Túl sok PNG-kép');const info=pngInfo(data);encoded+=data.length;if(['preview-light.png','preview-dark.png'].includes(name)){if(info.w>640||info.h>400)throw Error('Az előnézet legfeljebb 640×400 lehet');}else {decoded+=info.decoded;if(resources){const icon=doc.icons.find(icon=>icon.file===name);if(icon?(info.w!==96||info.h!==96):name==='loxone-logo.png'?(info.w>256||info.h>256):!(doc.version>=2?['background-light.png','background-dark.png','consumers-light.png','consumers-dark.png']:['background-light.png','background-dark.png']).includes(name)||info.w!==1280||info.h!==800)throw Error('Hibás Loxone-erőforráskép');}}}
 if(resources&&files.size!==(doc.version>=2?70:68)+(files.has('preview-light.png')?2:0))throw Error('Hiányzó Loxone-erőforráskép');
 if(files.has('preview-light.png')!==files.has('preview-dark.png'))throw Error('Mindkét előnézeti kép szükséges');
 if(encoded>5242880||decoded>(resources?20971520:12582912)||files.get('template.json').length>65536)throw Error('Memóriakorlát túllépve');
 for(const mode of ['light','dark']){
  const scene=doc[mode];if(!scene||!color(scene.background)||!Array.isArray(scene.elements)||scene.elements.length>96)throw Error('Hiányzó vagy hibás világos/sötét nézet');
  const hotspot=scene.home_hotspot;
  if(hotspot&&(!['x','y','w','h'].every(key=>Number.isInteger(hotspot[key]))||hotspot.x<0||hotspot.y<0||hotspot.w<1||hotspot.h<1||hotspot.x+hotspot.w>1280||hotspot.y+hotspot.h>800))throw Error('Hibás ház érintési terület');
  for(const [index,e] of scene.elements.entries()){
   if(!types.includes(e.type))throw Error('Nem támogatott elemtípus ('+mode+', #'+(index+1)+'): '+e.type+'. Firmware-frissítés szükséges.');
   if(!['x','y','w','h'].every(k=>Number.isInteger(e[k]))||e.x<0||e.y<0||e.w<1||e.h<1||e.x+e.w>1280||e.y+e.h>800)
    throw Error('Hibás elemhatár ('+mode+', #'+(index+1)+', '+e.type+'): '+e.x+','+e.y+' '+e.w+'×'+e.h);
   if(e.type==='solar_timeline'&&(e.w<480||e.h<132))throw Error('A napállás idővonal legalább 480×132 legyen');
   if(['value','flow','bar','gauge'].includes(e.type)&&typeof e.source!=='string')throw Error('Hibás adatforrás');
   for(const key of ['color','background','positive_color','negative_color','zero_color','shadow_color'])if(e[key]!==undefined&&!color(e[key]))throw Error('Hibás szín');
   if(e.foreground!==undefined&&(typeof e.foreground!=='boolean'||!['image','pulse'].includes(e.type)))throw Error('Hibás előtérréteg');
   if(e.soc_four_bands!==undefined&&(typeof e.soc_four_bands!=='boolean'||e.type!=='bar'||e.source!=='soc'))throw Error('Hibás SOC-színezés');
   if(['preview-light.png','preview-dark.png'].includes(e.file)||Object.values(e.files??{}).some(name=>['preview-light.png','preview-dark.png'].includes(name)))throw Error('Az előnézet nem lehet kijelzőikon');
   if(e.type==='image'&&(!safeAsset(e.file)||!files.has(e.file)))throw Error('Hiányzó PNG');
   if(e.type==='weather'&&e.files)for(const key of ['sunny','partly_cloudy','cloudy','rain','snow','night'])if(!safeAsset(e.files[key])||!files.has(e.files[key]))throw Error('Hiányzó időjárás ikon');
   if(e.type==='chart'&&!['production','consumption','grid','battery','soc','week','month','solar_hourly'].includes(e.kind))throw Error('Hibás grafikon típus');
   if(e.type==='flow'&&(!Array.isArray(e.points)||e.points.length<2||e.points.length>8||e.points.some(p=>!Number.isInteger(p.x)||!Number.isInteger(p.y)||p.x<0||p.y<0||p.x>=e.w||p.y>=e.h)))throw Error('Hibás áramlási pontok');
  }
 }
 return {encoded,decoded};
}
// A csomag a valódi LVGL-renderelővel készült képet tartalmazza.
function previewName(mode){return mode==='dark'?'preview-dark.png':'preview-light.png';}
function drawStoredPreview(canvas,bitmap){
 const ctx=canvas.getContext('2d');ctx.clearRect(0,0,canvas.width,canvas.height);
 if(bitmap)ctx.drawImage(bitmap,0,0,canvas.width,canvas.height);
}
if(typeof document!=='undefined'){
 const message=document.getElementById('message'),list=document.getElementById('list'),fileInput=document.getElementById('zip'),check=document.getElementById('check'),install=document.getElementById('install');
 let selected=null,verified=null,images=new Map(),generation=0,busy=false;
 function loadThumb(t,holder){
  const img=new Image();img.alt=t.name+' előnézete';img.className='thumb-image';
  img.loading='lazy';img.decoding='async';img.style.opacity=0;holder.append(img);
  img.onload=()=>{img.style.opacity=1;};
  img.onerror=()=>{img.remove();}; // Régi csomag: szándékosan üres keret.
  img.src='/api/templates/preview?id='+encodeURIComponent(t.id)+'&mode=light&v='+encodeURIComponent(t.version??0);
 }
 async function refresh(){
  const r=await fetch('/api/templates',{cache:'no-store'});if(!r.ok)throw Error(r.status);
  const data=await r.json();list.replaceChildren();
  for(const resource of data.resources||[]){
   const article=document.createElement('article');article.className='theme-card';
   const info=document.createElement('div');info.className='theme-info';
   const title=document.createElement('strong');title.textContent=resource.name;
   const caption=document.createElement('small');caption.textContent='Közös erőforráscsomag · v'+resource.version+' · a Loxone-oldalakhoz';
   const del=document.createElement('button');del.textContent='Törlés';del.onclick=()=>{if(confirm('Törlöd a Loxone-oldalak közös erőforrásait?'))action('/api/templates/delete',resource.id);};
   const holder=document.createElement('div');holder.className='theme-thumb';
   info.append(title,caption,del);article.append(holder,info);list.append(article);loadThumb(resource,holder);
  }
  for(const [index,t] of data.templates.entries()){
   const article=document.createElement('article');article.className='theme-card';
   const holder=document.createElement('div');holder.className='theme-thumb';
   const info=document.createElement('div');info.className='theme-info';
   const title=document.createElement('strong');title.textContent=t.name+(t.id===data.active?' ✓':'');
   const caption=document.createElement('small');caption.textContent=t.id==='builtin'?'Mindig az első helyen':'Téma';
   const actions=document.createElement('div');
   const button=document.createElement('button');button.textContent='Aktiválás';button.onclick=()=>action('/api/templates/select',t.id);
   actions.append(button);
   if(t.id!=='builtin'){
    for(const [symbol,offset,tip] of [['↑',-1,'Fel'],['↓',1,'Le']]){
     const move=document.createElement('button');move.textContent=symbol;move.title='Mozgatás: '+tip;
     move.setAttribute('aria-label',t.name+' '+tip);
     move.disabled=index+offset<1||index+offset>=data.templates.length;
     move.onclick=async()=>{
      const ids=data.templates.map(item=>item.id),swap=index+offset;
      [ids[index],ids[swap]]=[ids[swap],ids[index]];
      await action('/api/templates/reorder',JSON.stringify(ids));
     };
     actions.append(move);
    }

    const del=document.createElement('button');del.textContent='Törlés';
    del.onclick=()=>{if(confirm('Biztosan törlöd a sablont?'))action('/api/templates/delete',t.id);};
    actions.append(del);
   }
   info.append(title,caption,actions);article.append(holder,info);list.append(article);
   loadThumb(t,holder);
  }
 }
 function lock(value){busy=value;list.inert=value;list.setAttribute('aria-busy',String(value));message.className=value?'working':'';fileInput.disabled=value;check.disabled=value||!selected;install.disabled=value||verified!==selected||!selected;}
 async function action(path,body){if(busy)return;lock(true);message.textContent=path==='/api/templates/delete'?'Törlés folyamatban…':'Feldolgozás…';try{const r=await fetch(path,{method:'POST',headers:{'X-Zolee-Template':'1'},body});const text=await r.text();if(!r.ok)throw Error(text);message.textContent=text;await refresh();}catch(e){message.textContent=e.message;}finally{lock(false);}}
 fileInput.onchange=async()=>{const id=++generation;selected=null;verified=null;install.textContent='3. Telepítés';lock(false);for(const image of images.values())image.close();images=new Map();document.getElementById('previews').hidden=true;
  try{const file=fileInput.files[0];if(!file)return;message.textContent='Helyi előnézet készül…';const files=await unpackZip(await file.arrayBuffer());const manifest=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(files.get('template.json'))),memory=validateManifest(manifest,files);
   const next=new Map();try{for(const mode of ['light','dark']){const name=previewName(mode),data=files.get(name);if(data)next.set(name,await createImageBitmap(new Blob([data],{type:'image/png'})));}}catch(e){for(const image of next.values())image.close();throw e;}
   if(id!==generation){for(const image of next.values())image.close();return;}images=next;selected=file;
   document.getElementById('metadata').textContent=manifest.name+' · v'+manifest.version+' · PNG: '+(memory.encoded/1048576).toFixed(2)+' MiB · becsült dekódolt képmemória: '+(memory.decoded/1048576).toFixed(2)+' MiB';
   for(const mode of ['light','dark'])drawStoredPreview(document.getElementById(mode),images.get(previewName(mode)));document.getElementById('previews').hidden=false;
   message.textContent='Helyi előnézet kész. Következő lépés: ellenőrzés az eszközön.';
  }catch(e){if(id===generation)message.textContent='Helyi ellenőrzés sikertelen: '+e.message;}finally{if(id===generation)lock(false);}};
 check.onclick=async()=>{if(!selected||busy)return;const file=selected;verified=null;lock(true);message.textContent='Az eszköz ellenőrzi a ZIP-et, telepítés nélkül…';
  try{const r=await fetch('/api/templates/validate',{method:'POST',headers:{'X-Zolee-Template':'1'},body:file});if(!r.ok)throw Error(await r.text());const report=await r.json();message.textContent=report.message+' · v'+report.version+' · '+(report.decoded_image_bytes/1048576).toFixed(2)+' MiB';install.textContent=report.updating?'3. Meglévő téma frissítése':'3. Telepítés';if(report.valid&&report.installable&&file===selected)verified=file;}catch(e){message.textContent=e.message;}finally{lock(false);}};
 install.onclick=async()=>{if(selected&&verified===selected){const file=selected;const updating=install.textContent.includes('frissítése');if(updating&&!confirm('A meglévő téma új csomaggal frissül. Folytatod?'))return;verified=null;await action('/api/templates/install',file);lock(false);}};
 refresh().catch(()=>message.textContent='A sablonlista nem olvasható');
}
