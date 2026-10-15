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
 if(u16(end+4)||u16(end+6)||u16(end+8)!==count||!count||count>34||offset+cdSize!==end)throw Error('Nem támogatott ZIP');
 const files=new Map();let p=offset,total=0;
 for(let i=0;i<count;i++){
  if(p+46>end||u32(p)!==0x02014b50)throw Error('Hibás ZIP-könyvtár');
  const flags=u16(p+8),method=u16(p+10),crc=u32(p+16),compressed=u32(p+20),size=u32(p+24),n=u16(p+28),local=u32(p+42),mode=(u32(p+38)>>>16)&0xf000;
  if((flags&~(0x800|8|6))||(method!==0&&method!==8)||u16(p+34)||(mode&&mode!==0x8000)||!n||n>64||!size||size>2097152||(total+=size)>ZIP_LIMIT||p+46+n>end)throw Error('ZIP méretkorlát');
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
 const color=x=>/^#[0-9a-f]{6}$/i.test(x),types=['box','pulse','solar_disc','solar_arc','label','value','image','weather','bar','gauge','flow','chart'];
 let encoded=0,decoded=0,assets=0;
 for(const [name,data] of files)if(name!=='template.json'){if(++assets>32)throw Error('Túl sok PNG-kép');const info=pngInfo(data);encoded+=data.length;decoded+=info.decoded;}
 if(encoded>4194304||decoded>12582912||files.get('template.json').length>65536)throw Error('Memóriakorlát túllépve');
 for(const mode of ['light','dark']){
  const scene=doc[mode];if(!scene||!color(scene.background)||!Array.isArray(scene.elements)||scene.elements.length>96)throw Error('Hiányzó vagy hibás világos/sötét nézet');
  for(const [index,e] of scene.elements.entries()){
   if(!types.includes(e.type))throw Error('Nem támogatott elemtípus ('+mode+', #'+(index+1)+'): '+e.type+'. Firmware-frissítés szükséges.');
   if(!['x','y','w','h'].every(k=>Number.isInteger(e[k]))||e.x<0||e.y<0||e.w<1||e.h<1||e.x+e.w>1280||e.y+e.h>800)
    throw Error('Hibás elemhatár ('+mode+', #'+(index+1)+', '+e.type+'): '+e.x+','+e.y+' '+e.w+'×'+e.h);
   if(['value','flow','bar','gauge'].includes(e.type)&&typeof e.source!=='string')throw Error('Hibás adatforrás');
   for(const key of ['color','background','positive_color','negative_color'])if(e[key]!==undefined&&!color(e[key]))throw Error('Hibás szín');
   if(e.type==='image'&&(!safeAsset(e.file)||!files.has(e.file)))throw Error('Hiányzó PNG');
   if(e.type==='weather'&&e.files)for(const key of ['sunny','partly_cloudy','cloudy','rain','snow','night'])if(!safeAsset(e.files[key])||!files.has(e.files[key]))throw Error('Hiányzó időjárás ikon');
   if(e.type==='chart'&&!['production','consumption','grid','battery','soc','week','month','solar_hourly'].includes(e.kind))throw Error('Hibás grafikon típus');
   if(e.type==='flow'&&(!Array.isArray(e.points)||e.points.length<2||e.points.length>8||e.points.some(p=>!Number.isInteger(p.x)||!Number.isInteger(p.y)||p.x<0||p.y<0||p.x>=e.w||p.y>=e.h)))throw Error('Hibás áramlási pontok');
  }
 }
 return {encoded,decoded};
}
const demo={pv:7,home:2.1,grid:2.4,battery:2.5,soc:74,battery_temp:32,temperature:18.2,wind:12,production_today:30,consumption_today:29.9,import_today:9.8,export_today:7.5,charge_today:9.6,discharge_today:7.1,pv_home_today:12.9,pv_export_today:7.5,pv_stored_today:9.6,grid_home_today:9.8,battery_home_today:7.1,self_sufficiency_pct:67,self_consumption_pct:75,pv_home_pct:43,pv_load_pct:43,pv_export_pct:25,pv_stored_pct:32,grid_home_pct:33,battery_home_pct:24,purchase_day:637,revenue_day:37.5,savings_day:1100,purchase_month:19110,revenue_month:1125,savings_month:33000,grid_net_today:2.3,battery_net_today:-2.5};
function demoText(source){
 if(source==='clock')return '14:37';if(source==='date')return '2026.10.11.';if(source==='status')return 'Normál működés';
 if(source==='grid_direction')return 'Vételezés';if(source==='battery_direction')return 'Töltés';
 const value=demo[source]??1;const unit=source.includes('_pct')||source==='soc'?'%':source.includes('_today')?'kWh':source.includes('_day')||source.includes('_month')?'Ft':source==='wind'?'km/h':source.includes('temp')?'°C':'kW';
 return value.toLocaleString('hu-HU',{maximumFractionDigits:source==='soc'?0:1})+' '+unit;
}
function previewChart(ctx,e,palette){
 const x=e.x+35,y=e.y+8,w=e.w-45,h=e.h-35,signed=['grid','battery'].includes(e.kind),period=['week','month'].includes(e.kind);
 ctx.strokeStyle='#93a2ae55';ctx.lineWidth=1;for(let j=0;j<=24;j++){ctx.beginPath();ctx.moveTo(x+w*j/24,y);ctx.lineTo(x+w*j/24,y+h);ctx.stroke();}
 for(let j=0;j<3;j++){ctx.beginPath();ctx.moveTo(x,y+h*j/2);ctx.lineTo(x+w,y+h*j/2);ctx.stroke();}
 if(period){for(let j=0;j<(e.kind==='week'?7:31);j++)for(let k=0;k<3;k++){ctx.fillStyle=[palette.pv,palette.home,palette.grid][k];const width=w/(e.kind==='week'?7:31);ctx.fillRect(x+j*width+k*width*.22,y+h-(.3+.45*Math.abs(Math.sin(j*.4+k)))*h,width*.18,(.3+.45*Math.abs(Math.sin(j*.4+k)))*h);}}
 else{
  const points=[];for(let j=0;j<=60;j++){const a=j/60;let value=e.kind==='soc'?.2+.8*Math.max(0,Math.sin((a-.25)*Math.PI*1.5)):signed?.7*Math.sin(a*Math.PI*3):Math.max(0,Math.sin((a-.25)*Math.PI*2))*(.6+.15*Math.sin(j));points.push([x+a*w,y+h-(signed?(1+value)/2:value)*h,value]);}
  if(signed){
   const zero=y+h/2;
   for(let j=1;j<points.length;j++){
    const a=points[j-1],b=points[j],parts=a[2]*b[2]<0?2:1,cross=[a[0]+(b[0]-a[0])*(parts===2?a[2]/(a[2]-b[2]):0),zero,0];
    for(let part=0;part<parts;part++){
     const start=part?cross:a,end=parts===2&&part===0?cross:b,sign=part?b[2]:a[2]+(parts===1?b[2]:0);
     ctx.fillStyle=ctx.strokeStyle=e.kind==='battery'?palette.battery:sign<0?palette.home:palette.grid;
     ctx.globalAlpha=.5;ctx.beginPath();ctx.moveTo(start[0],start[1]);ctx.lineTo(end[0],end[1]);ctx.lineTo(end[0],zero);ctx.lineTo(start[0],zero);ctx.closePath();ctx.fill();ctx.globalAlpha=1;
     ctx.beginPath();ctx.moveTo(start[0],start[1]);ctx.lineTo(end[0],end[1]);ctx.stroke();
    }
   }
   ctx.font='12px system-ui';ctx.fillStyle='#81909a';ctx.fillText('00:00       06:00       12:00       18:00',x,y+h+16);return;
  }
  const series=['production','consumption'].includes(e.kind)?3:1;
  for(let k=series-1;k>=0;k--){ctx.fillStyle=ctx.strokeStyle=e.kind==='soc'||e.kind==='battery'?palette.battery:e.kind==='grid'?palette.grid:[palette.pv,palette.home,palette.battery][k];ctx.globalAlpha=.5;ctx.beginPath();ctx.moveTo(x,y+h*(signed?.5:1));for(let [xx,yy] of points)ctx.lineTo(xx,y+h-(y+h-yy)*(1-k*.2));ctx.lineTo(x+w,y+h*(signed?.5:1));ctx.closePath();ctx.fill();ctx.globalAlpha=1;}
 }
 ctx.font='12px system-ui';ctx.fillStyle='#81909a';ctx.fillText(period?'kWh':'00:00       06:00       12:00       18:00',x,y+h+16);
}
function drawPreview(canvas,scene,images){
 const ctx=canvas.getContext('2d');ctx.clearRect(0,0,1280,800);ctx.fillStyle=scene.background;
 if(scene.gradient){let gradient=ctx.createLinearGradient(0,0,1280,0);gradient.addColorStop(0,scene.background);gradient.addColorStop(1,scene.gradient);ctx.fillStyle=gradient;}ctx.fillRect(0,0,1280,800);
 const palette=scene.palette||{pv:'#38b830',home:'#f86810',grid:'#e53935',battery:'#0060c0'};
 for(const e of scene.elements){ctx.save();ctx.beginPath();ctx.rect(e.x,e.y,e.w,e.h);ctx.clip();ctx.fillStyle=e.color||'#e7ece9';ctx.strokeStyle=e.color||palette.pv;
  if(e.type==='box'){ctx.globalAlpha=(e.opacity??255)/255;ctx.fillStyle=e.background||'#ffffff';ctx.beginPath();ctx.roundRect(e.x,e.y,e.w,e.h,e.radius??16);ctx.fill();}
  else if(e.type==='image'||e.type==='weather'){let img=images.get(e.type==='image'?e.file:e.files?.sunny);if(img)ctx.drawImage(img,e.x,e.y,e.w,e.h);}
  else if(e.type==='label'||e.type==='value'){
   const text=e.type==='label'?e.text:demoText(e.source);ctx.font=(e.font??20)+'px system-ui';ctx.textBaseline='top';ctx.textAlign=e.align==='center'?'center':'left';
   if(e.text_shadow){ctx.shadowColor='#000000';ctx.shadowBlur=1;ctx.shadowOffsetX=e.shadow_offset??1;ctx.shadowOffsetY=e.shadow_offset??1;}
   ctx.fillText(text,e.align==='center'?e.x+e.w/2:e.x,e.y);
  }else if(e.type==='flow'){ctx.beginPath();ctx.moveTo(e.x+e.points[0].x,e.y+e.points[0].y);if(e.curve==='bezier')for(let i=1;i+2<e.points.length;i+=3)ctx.bezierCurveTo(e.x+e.points[i].x,e.y+e.points[i].y,e.x+e.points[i+1].x,e.y+e.points[i+1].y,e.x+e.points[i+2].x,e.y+e.points[i+2].y);else for(let p of e.points.slice(1))ctx.lineTo(e.x+p.x,e.y+p.y);ctx.lineCap='round';ctx.lineJoin='round';ctx.lineWidth=e.radius??3;ctx.shadowBlur=e.glow??0;ctx.shadowColor=e.color;ctx.stroke();}
  else if(e.type==='solar_arc'){
   let isNight=e.night_arc===true;ctx.beginPath();
   for(let k=0;k<=64;k++){
    const theta=Math.PI*(isNight?(1+k/64):(1-k/64));
    const px=e.x+(e.center_x??e.w/2)+(e.radius_x??e.w/2)*Math.cos(theta);
    const py=e.y+(e.center_y??e.h/2)-(e.radius_y??e.h/2)*Math.sin(theta);
    if(!k)ctx.moveTo(px,py);else ctx.lineTo(px,py);
   }
   ctx.strokeStyle=e.color??(isNight?'#56a5ff':'#ffd04b');ctx.globalAlpha=isNight?.55:1;
   ctx.lineWidth=e.radius??3;ctx.stroke();
  }
  else if(e.type==='solar_disc'){ctx.shadowBlur=22;ctx.shadowColor='#ffd453';ctx.fillStyle='#ffcc35';ctx.beginPath();ctx.arc(e.x+e.w/2,e.y+e.h/2,e.w/2,0,2*Math.PI);ctx.fill();}
  else if(e.type==='pulse'){ctx.shadowBlur=e.glow??20;ctx.shadowColor=e.color;ctx.beginPath();ctx.arc(e.x+e.w/2,e.y+e.h/2,e.w/2,0,2*Math.PI);ctx.fill();}
  else if(e.type==='bar')ctx.fillRect(e.x,e.y,e.w*.74,e.h);
  else if(e.type==='gauge'){ctx.lineWidth=14;ctx.beginPath();ctx.arc(e.x+e.w/2,e.y+e.h/2,e.w/2-12,.75*Math.PI,2*Math.PI);ctx.stroke();}
  else if(e.type==='chart')previewChart(ctx,e,palette);
  ctx.restore();
 }
}
if(typeof document!=='undefined'){
 const message=document.getElementById('message'),list=document.getElementById('list'),fileInput=document.getElementById('zip'),check=document.getElementById('check'),install=document.getElementById('install');
 let selected=null,verified=null,images=new Map(),generation=0,busy=false;
 async function loadThumb(t,holder){
  if(t.id==='builtin'){holder.textContent='Alapnézet';return;}
  const canvas=document.createElement('canvas');canvas.width=1280;canvas.height=800;canvas.className='thumb-canvas';
  holder.append(canvas);
  try{
   const r=await fetch('/api/templates/manifest?id='+encodeURIComponent(t.id),{cache:'no-store'});
   if(!r.ok)throw Error('Nincs elérhető előnézet');
   const manifest=await r.json();drawPreview(canvas,manifest.light,new Map());
   /* A külön mellékelt, tömörített PNG előnyt élvez a böngésző által
    * készített szerkezeti előnézettel szemben. */
   const img=new Image();img.alt=t.name+' előnézete';img.className='thumb-image';
   img.onload=()=>{if(holder.isConnected){holder.append(img);canvas.hidden=true;}};
   img.src='/api/templates/preview?id='+encodeURIComponent(t.id)+'&mode=light';
  }catch(e){holder.textContent='Előnézet nem érhető el';}
 }
 async function refresh(){
  const r=await fetch('/api/templates',{cache:'no-store'});if(!r.ok)throw Error(r.status);
  const data=await r.json();list.replaceChildren();
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
   await loadThumb(t,holder);
  }
 }
 function lock(value){busy=value;fileInput.disabled=value;check.disabled=value||!selected;install.disabled=value||verified!==selected||!selected;}
 async function action(path,body){if(busy)return;lock(true);message.textContent='Feldolgozás…';try{const r=await fetch(path,{method:'POST',headers:{'X-Zolee-Template':'1'},body});const text=await r.text();if(!r.ok)throw Error(text);message.textContent=text;await refresh();}catch(e){message.textContent=e.message;}finally{lock(false);}}
 fileInput.onchange=async()=>{const id=++generation;selected=null;verified=null;install.textContent='3. Telepítés';lock(false);for(const image of images.values())image.close();images=new Map();document.getElementById('previews').hidden=true;
  try{const file=fileInput.files[0];if(!file)return;message.textContent='Helyi előnézet készül…';const files=await unpackZip(await file.arrayBuffer());const manifest=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(files.get('template.json'))),memory=validateManifest(manifest,files);
   const next=new Map();try{for(const [name,data] of files)if(safeAsset(name))next.set(name,await createImageBitmap(new Blob([data],{type:'image/png'})));}catch(e){for(const image of next.values())image.close();throw e;}
   if(id!==generation){for(const image of next.values())image.close();return;}images=next;selected=file;
   document.getElementById('metadata').textContent=manifest.name+' · v'+manifest.version+' · PNG: '+(memory.encoded/1048576).toFixed(2)+' MiB · becsült dekódolt képmemória: '+(memory.decoded/1048576).toFixed(2)+' MiB';
   drawPreview(document.getElementById('light'),manifest.light,images);drawPreview(document.getElementById('dark'),manifest.dark,images);document.getElementById('previews').hidden=false;
   message.textContent='Helyi előnézet kész. Következő lépés: ellenőrzés az eszközön.';
  }catch(e){if(id===generation)message.textContent='Helyi ellenőrzés sikertelen: '+e.message;}finally{if(id===generation)lock(false);}};
 check.onclick=async()=>{if(!selected||busy)return;const file=selected;verified=null;lock(true);message.textContent='Az eszköz ellenőrzi a ZIP-et, telepítés nélkül…';
  try{const r=await fetch('/api/templates/validate',{method:'POST',headers:{'X-Zolee-Template':'1'},body:file});if(!r.ok)throw Error(await r.text());const report=await r.json();message.textContent=report.message+' · v'+report.version+' · '+(report.decoded_image_bytes/1048576).toFixed(2)+' MiB';install.textContent=report.updating?'3. Meglévő téma frissítése':'3. Telepítés';if(report.valid&&report.installable&&file===selected)verified=file;}catch(e){message.textContent=e.message;}finally{lock(false);}};
 install.onclick=async()=>{if(selected&&verified===selected){const file=selected;const updating=install.textContent.includes('frissítése');if(updating&&!confirm('A meglévő téma új csomaggal frissül. Folytatod?'))return;verified=null;await action('/api/templates/install',file);lock(false);}};
 refresh().catch(()=>message.textContent='A sablonlista nem olvasható');
}
