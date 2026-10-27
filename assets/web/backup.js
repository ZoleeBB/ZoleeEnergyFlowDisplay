'use strict';
const $=id=>document.getElementById(id);
let selected=null,busy=false;
const headers={'X-Zolee-Settings':'1','Content-Type':'application/json'};
function setBusy(value){busy=value;$('check').disabled=$('restore').disabled=value||!selected;$('flash-save').disabled=$('flash-restore').disabled=value;}
async function call(url,body){
 const response=await fetch(url,{method:'POST',headers,body:body===undefined?'{}':body,cache:'no-store'});
 let data={};try{data=await response.json();}catch(e){data={ok:false,message:'Váratlan válasz ('+response.status+').'};}
 if(!response.ok&&data.ok!==false)data.ok=false;return data;
}
function waitForRestart(target){
 let left=25;target.textContent='Sikeres visszaállítás. Az eszköz újraindul, újracsatlakozás '+left+' s múlva.';
 const timer=setInterval(async()=>{left--;
  if(left>0){target.textContent='Újraindítás, kapcsolódás '+left+' s múlva.';return;}
  clearInterval(timer);target.textContent='Kapcsolódás az újraindult eszközhöz…';
  const retry=async()=>{try{const r=await fetch('/?ready='+Date.now(),{cache:'no-store'});if(r.ok){location.replace('/');return;}}catch(e){}setTimeout(retry,2000);};retry();
 },1000);
}
$('file').addEventListener('change',()=>{
 const file=$('file').files[0];selected=null;$('message').textContent='';
 if(!file){setBusy(false);return;}
 if(file.size>262144){$('message').textContent='A fájl túl nagy (legfeljebb 256 KiB).';setBusy(false);return;}
 file.text().then(text=>{selected=text;setBusy(false);$('message').textContent='Kiválasztva: '+file.name+'. Előbb ellenőrizd, majd állítsd vissza.';});
});
$('check').addEventListener('click',async()=>{
 if(!selected||busy)return;setBusy(true);$('message').textContent='Ellenőrzés…';
 try{const data=await call('/api/backup/validate',selected);$('message').textContent=data.ok?data.message+' ('+data.files+' fájl)':'Hiba: '+data.message;}
 catch(e){$('message').textContent='Kapcsolati hiba.';}finally{setBusy(false);}
});
$('restore').addEventListener('click',async()=>{
 if(!selected||busy)return;
 if(!confirm('A mentésben lévő beállítások felülírják a jelenlegieket, majd az eszköz újraindul. Folytatod?'))return;
 setBusy(true);$('message').textContent='Visszaállítás…';
 try{const data=await call('/api/backup/restore',selected);if(data.ok&&data.restart){waitForRestart($('message'));return;}$('message').textContent='Hiba: '+data.message;}
 catch(e){$('message').textContent='Kapcsolati hiba.';}
 setBusy(false);
});
async function refreshFlash(){
 try{const r=await fetch('/api/backup/flash',{cache:'no-store'});const d=await r.json();
  $('flash-state').textContent=!d.supported?'Nem érhető el':d.available?'Elérhető':'Még nincs mentés';
  $('flash-time').textContent=d.available&&d.epoch>1577836800?new Date(d.epoch*1000).toLocaleString('hu-HU'):'—';
  $('flash-size').textContent=d.available?(d.bytes/1024).toFixed(1)+' KiB':'—';
  $('flash-restore').disabled=busy||!d.available||!d.sd;$('flash-save').disabled=busy||!d.sd;
  if(!d.sd)$('flash-message').textContent='SD-kártya nélkül a mentés és a visszaállítás nem érhető el.';
 }catch(e){$('flash-state').textContent='Nem tölthető be';}
}
$('flash-save').addEventListener('click',async()=>{
 if(busy)return;setBusy(true);$('flash-message').textContent='Mentés…';
 try{const d=await call('/api/backup/flash');$('flash-message').textContent=d.ok?d.message:'Hiba: '+d.message;}catch(e){$('flash-message').textContent='Kapcsolati hiba.';}
 setBusy(false);refreshFlash();
});
$('flash-restore').addEventListener('click',async()=>{
 if(busy||!confirm('A belső tartalék felülírja az SD-kártya beállításait, majd az eszköz újraindul. Folytatod?'))return;
 setBusy(true);$('flash-message').textContent='Visszaállítás…';
 try{const d=await call('/api/backup/flash/restore');if(d.ok&&d.restart){waitForRestart($('flash-message'));return;}$('flash-message').textContent='Hiba: '+d.message;}
 catch(e){$('flash-message').textContent='Kapcsolati hiba.';}
 setBusy(false);
});
setBusy(false);refreshFlash();
