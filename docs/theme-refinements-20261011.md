# Témák vizuális finomítása — 2026-10-11

## Megvalósítás

- Élő otthon: fogyasztás a (650,526) csomópont alatt; fehér/sárga pulzáló pont; Töltöttség felirat; mai visszatáplálás az alsó sáv harmadik mezőjében. A sáv 20 képponttal lejjebb, kizárólag ez kap 58/255 áttetsző fehér buborékot.
- Energiautak: négypontos köbös Bézier-görbék, egyetlen vektoros stroke, lekerekített csatlakozások és végek. Nincs szegmensenként egymásra rajzolt áttetszőség. A mozgó pontok ugyanannak a görbének a mintavételezett pontjait követik. Halvány fotóháttér, a nappali képből generált éjszakai változat.
- Műszerfal: hálózati import az eredeti szín, export #FFEB38; akkutöltés az eredeti szín, leadás #2BB1FE. Finom áramköri fotóháttér, egyező éjszakai kép.
- Napi mérleg: a Tárolt és Hálózat jelmagyarázata mellől eltűnt a két felesleges ikon.
- Nyugodt otthon, Energiakör, Energiafőkönyv: egységes realisztikus ház-, napelem-, villanyoszlop- és akkumulátor-PNG-k. Nyugodt otthon új, részletes hatállapotú időjáráskészletet használ; a statisztika fejlécében ugyanaz a készlet látható.
- Energiafőkönyv: zöldövezeti európai városfotó, ugyanaz a város éjjel, megvilágított ablakokkal és utcákkal.
- Energiastatisztika: élénkebb szerepszínek, fogyasztás narancs, import a heti/havi diagramon is a hálózat színe. Hálózati egyenleg és Akku egyenleg helyes címek; Töltöttség egységes felirat. Időjárás, hőmérséklet és szél az óra előtt.

## Statisztikai ellenőrzés

Az élő teljesítmények kW/W, napi energiaösszegek kWh, SOC százalék, napi görbék ötperces átlagok. Hálózat: import pozitív, export negatív. Akkumulátor: leadás pozitív, töltés negatív. A napi hálózati egyenleg import mínusz export, az akkuegyenleg leadás mínusz töltés; ezek előjele megmarad. Nullán a hálózat Nincs áramlás feliratot mutat.

A termelési diagram rétegei tárolóba / házhoz / hálózatra, a fogyasztási diagramé hálózatról / termelésből / tárolóból. A termelési és fogyasztási százalékok saját napi összegükkel osztanak. A forrásfelosztás a meglévő PV-első becslés, ezt a lábléc jelöli; a bemenet nem azonosítja a korábban hálózatról töltött akkuenergia eredetét. Heti/havi oszlopok kWh-ban, a mai nap részleges. Adathiány és hiányzó SOC nem kap fiktív összekötést.

## Képek és memória

A képek a beépített image_gen eszközzel készültek. A három háttérpár 1024×640 RGB PNG, a felület 1280×800-ra méretezi őket. Az ikonok 128×128 RGBA PNG-k, a generált alfa megőrzésével. A sabloncsomagok a meglévő 4 MiB kódolt és 12 MiB dekódolt korlátokon belül maradnak. Az eredeti és a futásidejű képek SHA256-értékeit a theme-refinement-assets.json rögzíti.

## Validálás

A teljes helyi C/Python/JavaScript regressziós csomag sikeres: adatkezelés, telepítés, ZIP, PNG, webes validálás és kiadási csomagolás. A tényleges LVGL-firmware felület szigorú figyelmeztetésekkel fordul; 16 nappali/éjszakai render, mozgási irány, nulla/7 kW korlát, időjárásváltás, irányfüggő műszerszínek és pulzálás ellenőrzött. Az új kérés szerint a rendszertéma összegzője 116 helyett 110 képpont magas, változatlan tartalomtérrel (az alsó padding 8 helyett 2). Alatta a kilenc témát jelző pontok látszanak; a többi elrendezés és a rendszerikonok változatlanok.

A távoli feltöltés az explicit fejlesztési jogosultság tisztázása után megtörtént. A végleges GitHub CI-eredmények és a firmware-csomagok státusza a PR #6 leírásában követhető. Fizikai ESP32-P4 átvételi próba szükséges.

## Második finomítás

A hálózat görbéje a háttérképen lévő oszlop felső vezetékétől (136,178) indul. Az Élő otthon buborékja középen, 140 képpontos oldalmargókkal jelenik meg; három középre igazított oszlopot tartalmaz. Az Energiautak, Műszerfal és Főkönyv maszkolása nappal 174/255, éjjel 164/255; a mezők 150/255 opacitásúak. A Főkönyvben hatállapotú időjárásikon, hőmérséklet és széladat kerül az óra elé. A statisztikai akkuegyenleg pozitív görbéje és kitöltése #2BB1FE alapszínű, a negatív rész megtartja a témaszínt.

## Harmadik finomítás

- Élő otthon összegzőbuborék: 76/255 opacitás. Jobb felső sarokban hatállapotú realisztikus időjárás, hőmérséklet és szél. Hálózati exportnál az út, pontok, teljesítmény és irányfelirat #FFEB38; akkuleadás az adott mód korábbi világoskék színe, töltés #1875D1. A színváltás élő adatfrissítéskor történik, a fekete szövegárnyék megmarad.
- Energiautak: mezők 130/255 opacitással; időjárás és szél az óra előtt.
- Műszerfal: egy képpontos kontúr a műszerek aktív és háttérívén, valamint a SOC-sávon. Nappal #21343E, éjjel #DEEAF2. Az alatta rajzolt ív azonos középvonalat és értéket követ.
- Energiakör: négy út közös csomópontja (640,366), 32×32 pulzáló világos ponttal. A középső mező eltűnik; Töltöttség és a kisebb, 20-as érték az akku alatt.
- Energiafőkönyv: mezők 130/255 opacitással.

A meglévő, generált időjárásképek kerültek az új helyekre; nem készültek eltérő stílusú változatok. A regressziós csomag, a szigorú LVGL-fordítás és a 16 témanézet sikeres. Irányváltási teszt ellenőrzi a vonal/pont/érték/felirat színeit, a kontúr színét és az ívek szinkronértékét.

## Negyedik finomítás

Élő otthon: egyszerű témacím; időjárás 64×64, két közvetlenül egymás alatti 20-as értékkel, feljebb helyezve. Energiautak: piros (#F04438) házirány, az inverter takarja a csomópontot; hálózati export #FFEB38, akkutöltés #1875D1, leadás #2BB1FE. A fejléc időjárásikonjai az értékpár 60 képpontos magasságához igazodnak (Energiautak, Főkönyv, Statisztika).

Műszerfal: 280 ms ease-out értékanimáció, retarget az aktuális állásból; első adat azonnal megjelenik, a kontúr ugyanazt az animált értéket követi. Az LVGL objektumtörlés automatikusan törli az objektumhoz kötött animációt. A beállítás sablononként animate_ms, 0–1000 ms; alapértelmezetten 0.

Napi mérleg, Energiakör és Főkönyv: összes leadás #2BB1FE. Nyugodt otthon: három arányos napi oszlop, új citromsárga visszatáplálás. Energiakör: piros házirány, narancs import, sárga export, sötétkék töltés és világoskék leadás. Statisztika: negatív akkuegyenleg #1565C0, pozitív #2BB1FE. A rendszer téma változatlan ebben a körben.

A teljes helyi tesztcsomag, szigorú fordítás és 16 render sikeres. Külön animációteszt igazolja a köztes állást, ugrás nélküli retargetet, célértéket és a kontúr szinkronját mindkét módban.
