# Megjelenési rétegek

Az átdolgozott látványterv a felület hangulatát és a 720×720-as elrendezést mutatja.
Nem firmware-képernyőkép és nem összeégetett végleges UI-erőforrás.

| Réteg | Tartalom | Tervezett megvalósítás |
| --- | --- | --- |
| 0 – háttér | Nappali/éjjeli virtuális táj: felül égbolt, alul zöldövezet | Külön 720×720-as bitmap a firmware-ben, szöveg és energiadiagram nélkül |
| 1 – energiautak | Eredeti négyágú kereszt, aktív/inaktív vonalak, mozgó pontok | LVGL objektumok a háttér előtt |
| 2 – ikonok | Eredeti hálózat, akkumulátor, napelem, ház és szürke villámos hub | A fő Rendszer témából újrahasznált PNG-k |
| 3 – adatok | Fejléc, kW/W, SOC, akkuhőmérséklet, időjárás és óra | Dinamikus LVGL feliratok és időjárásikon |
| 4 – állapot | Masterkeresés, PIN-pad és diagnosztika | Külön állapotnézetek, későbbi implementáció |

A teljes látványterv nem használható háttérképként: az adatok és az ikonok
mindig külön rétegen maradnak. A nappali és éjjeli háttér csak az elfogadott
irányból, önálló erőforrásként készül el. A `NightMode` az egész megjelenést,
a szöveg/útvonal színeit és a fényerőt együtt váltja.

A táj komponálása biztosítja a kontrasztot: a feliratok mögött nyugodt égbolt
vagy egyenletes rét marad. Nem kerül a képre maszkoló fátyol, fehér/sötét kártya,
üveglap vagy a szöveg mögötti áttetsző panel. A ház a lenti zöldövezetbe illeszkedik;
a működő ház-ikon külön UI-erőforrás marad, nem kell a háttérbe beégetni.

A hub lényegesen kisebb, kb. 30 pixel átmérőjű. A kb. 88 pixeles külső ikonok
a szélekhez közelebb kerülnek, hogy az egyenes energiautak hossza kb. 175–210 pixel
lehessen. Az akku és a napelem adatai az ikonjuk alatt, a hálózat és a ház adatai
az ikonjuk jobb oldalán kapnak helyet. Az ikonok mögött sincs nagy, kitöltött lap.

A fejléc és az energiaértékek éles natív betűkkel készülnek. A végleges pixelpontos
elhelyezést a 720×720-as LVGL render és a valódi panel ellenőrzi.
