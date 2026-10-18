# Megjelenési rétegek

Az első látványterv a felület hangulatát és a 720×720-as elrendezést mutatja.
Nem firmware-képernyőkép és nem összeégetett végleges UI-erőforrás.

| Réteg | Tartalom | Tervezett megvalósítás |
| --- | --- | --- |
| 0 – háttér | Világos/sötét színes, nyugodt absztrakt háttér | Külön 720×720-as bitmap a firmware-ben, szöveg és ikon nélkül |
| 1 – energiautak | Eredeti négyágú kereszt, aktív/inaktív vonalak, mozgó pontok | LVGL objektumok a háttér előtt |
| 2 – ikonok | Eredeti hálózat, akkumulátor, napelem, ház és szürke villámos hub | A fő Rendszer témából újrahasznált PNG-k |
| 3 – adatok | Fejléc, kW/W, SOC, akkuhőmérséklet, időjárás és óra | Dinamikus LVGL feliratok és időjárásikon |
| 4 – állapot | Masterkeresés, PIN-pad és diagnosztika | Külön állapotnézetek, későbbi implementáció |

A teljes látványterv nem használható háttérképként: az adatok és az ikonok
mindig külön rétegen maradnak. A nappali és éjjeli háttér csak az elfogadott
irányból, önálló erőforrásként készül el. A `NightMode` az egész megjelenést,
a szöveg/útvonal színeit és a fényerőt együtt váltja.

A középső terület kevés részletet és gyenge kontrasztot kap; a színes részletek
főleg a széleken jelennek meg. Nincs a diagramra ráfestett táj, extra ház vagy
másik energiahálózat. Az egyenes energiautak alakja a meglévő kártyáét követi.

A fejléc és az energiaértékek éles natív betűkkel készülnek. A végleges pixelpontos
elhelyezést a 720×720-as LVGL render és a valódi panel ellenőrzi.
