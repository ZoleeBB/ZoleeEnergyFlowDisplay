# Akku diagnosztika

A `10-battery-diagnostics` téma a normál használat közben méri a töltési és
kisütési energiát, összeveti a SOC-változással, és a lezárt kisütésekből
kapacitást becsül. A háttér a ház külső fala mellett álló, fehér moduláris
akkumulátort mutatja természetes, kertes környezetben. Nappal és éjjel ugyanaz
a ház, akku és kert látható; csak a napszak és a világítás változik. A számokat a firmware rajzolja, nem a háttér részei.

| Nappal | Éjjel |
|---|---|
| ![Nappal](../templates/10-battery-diagnostics/preview-light.png) | ![Éjjel](../templates/10-battery-diagnostics/preview-dark.png) |

## Beállítás és telepítés

1. A webes beállításoknál add meg az `energy.battery_capacity_kwh` értékét.
   A gyári energiatároló-kapacitás **kWh**, a pillanatnyi teljesítmény **kW**.
   A 0 ismeretlen kapacitást jelent; ekkor a téma beállítást kér, nem értékel.
2. Frissítsd a firmware-t az ezt a témát támogató verzióra, majd telepítsd a
   `10-battery-diagnostics.zip` csomagot a webes sablonkezelővel, vagy másold az
   azonos nevű mappát az SD `/templates/` könyvtárába. [Részletes telepítés](templates.md).
3. Válaszd ki az **Akku diagnosztika** témát. A mérés másik téma megjelenítése
   közben is folytatódik; nincs hozzá külön adatküldés vagy invertervezérlés.

Masteren MQTT, HTTP és Loxone módban működik. A 10.1-es, SD-s Slave saját
folytonos mintáiból készít külön eredményt, ezért az eltérő kimaradások miatt
eltérhet a Mastertől. SD nélküli Slave csak a Rendszer témát használhatja;
a BLE statisztikai protokollt ez a fejlesztés nem változtatja. A különálló
720×720-as 4B firmware nem kapja meg ezt az 1280×800-as témát.

## Mit jelentenek az értékek?

Legyen `C` a beállított névleges kWh, `ΔSOC` a szakaszban jelzett töltöttségváltozás
százalékpontban, `Ebe` a betöltött és `Eki` a leadott kWh.

| Mutató | Számítás és jelentés |
|---|---|
| Ma betöltött / kivett energia | A meglévő napi, trapézszabállyal integrált kWh. Alatta `100 × E / C`, a ténylegesen megmért naprészekből. Több ciklusnál meghaladhatja a 100%-ot. |
| Töltési energia / SOC | `100 × Ebe / (C × ΔSOC / 100)`. A 105% azt jelenti, hogy a SOC-emelkedés névleges energiájánál 5%-kal több jutott a mérési ponton be. Ez önmagában nem kapacitásromlás. |
| Leadott energia / SOC | `100 × Eki / (C × ΔSOC / 100)`. Például 15 kWh névleges, 60 pp csökkenés és 8,28 kWh leadás: 92%. |
| Becsült kivehető kapacitás | Lezárt kisütésenként `100 × Eki / ΔSOC`; a megtartott, érvényes kisütések mediánja. Példában 13,8 kWh, a névleges 92%-a. |
| Élő szakasz | Az aktuális töltés/kisütés ΔSOC és kWh értéke. A még futó szakasz aránya `~` előtagot kap; a mediánba lezárás után kerül. |
| SOC-eltérés | `ΔSOC − 100 × E / C`, százalékpont. +1,6 pp: ennyivel többet mozdult a jelzett SOC, mint amennyit a névleges kapacitásból a mért energia indokolna. |
| Párosított hatásfok | `100 × Eki / Ebe`, kizárólag azonos SOC-ablakú, szomszédos töltés/kisütés legutóbbi lezárt párjából. Nem a napi két összeg hányadosa. |
| Pillanatnyi C-ráta | `abs(PowerBat) / C`. Például 3 kW / 15 kWh = 0,20 C. |
| Megfigyelt ciklusok | `(összes megfigyelt Ebe + Eki) / (2 × C)`, EFC. Csak a diagnosztika indulása óta megmért energia; nem a BMS élettartam-ciklusszáma. |

Az élő SOC, teljesítmény és akkuhőmérséklet a bal alsó kártyán jelenik meg.
PowerBat negatív: töltés; pozitív: kisütés. Az előjelváltást a nullátmetszésnél
kettévágjuk, így a két irány energiája nem oltja ki egymást.

## Kiértékelés és adatminőség

- Egy kapacitásméréshez legalább **20 pp SOC**, **10 perc** és legalább a
  névleges kapacitás 2%-ának megfelelő mért energia kell. A névleges 1%-át
  mozgató rövid szakaszból nem számolunk 100%-ra vetített kapacitást.
- Irányváltás vagy 10 percnyi, legfeljebb 10 W-os készenlét zárja a szakaszt.
  A kis teljesítmény energiája az összegzőkben szerepel, de ebből nem készül
  kapacitásbecslés. Hosszú készenlétet nem kötünk össze egy későbbi terheléssel.
- Legfeljebb **hét lezárt töltési/kisütési szakasz** marad meg. A 30 napnál
  régebbi és a jövőbeli eredményeket nem értékeljük. A kisütések mediánja
  csökkenti egyetlen eltérő mérés hatását; nem jelent laboratóriumi pontosságot.
- Az értékelés előzetes, amíg nincs legalább három, egyenként legalább
  **60 pp-s kisütés** a megtartott előzményben. Ezután 80% alatt alacsony mért
  energiaarányt, 80–110% között megfelelő arányt, 110% felett a referencia vagy
  a mérési pont ellenőrzését jelzi. Ezek tájékoztató szoftveres határok,
  nem gyártói garanciális küszöbök és nem bizonyított elhasználódás.
- Hiányzó referencia vagy még kevés mérés esetén `--` és mérésgyűjtési üzenet
  látszik. Nincs automatikus 100%-os „egészséges” alapérték vagy 100%-ra vágott arány.
- A hatásfokhoz mindkét szakasz minősített, kezdő/záró SOC-juk legfeljebb
  1 pp-t térhet el, a kisütés a töltés végétől két napon belül fejeződik be,
  nincs közben adatfolyam-megszakítás, és `Eki ≤ Ebe`. Más esetben nincs új
  hatásfok-eredmény; a korábbi, legfeljebb 30 napos érték marad látható.

Újraindítás, megszakított adatkapcsolat, konfigurált adatfogadási timeoutnál
hosszabb mintaköz, visszafelé haladó idő, 2 másodpercnél nagyobb óraeltérés,
irreális vagy több mint 2 pp-s ellenirányú SOC-mozgás eldobja az aktuális
szakaszt. Az óra/SOC-védelem a folyamatosan megfigyelt kapacitásmérést védi;
nem számol hiányzó intervallumra feltételezett energiát. A feldolgozáshoz
szinkronizált idő szükséges. Az éjfél önmagában nem szakítja meg a szakaszt.

## Mit lehet ebből megállapítani?

A téma a **mérési ponton kivehető energia és a jelzett SOC összhangját** vizsgálja.
Az AC oldali PowerBat az inverter veszteségét is tartalmazhatja. A gyári teljes
kapacitás és a BMS által engedett használható SOC-ablak eltérhet. Hideg/meleg,
terhelés, SOC-kvantálás és BMS-újrakalibrálás is befolyásolja a becslést.
Az alacsony arány ezek vagy tényleges kapacitáscsökkenés következménye is lehet.
Azonos beállítással, hasonló hőmérsékleten/terheléssel megismételt nagy SOC-ablakú
szakaszok összehasonlítása informatívabb egy rövid nappali részletnél.

Feszültség-, áram- és cellaszintű adatok nélkül belső ellenállást, cellaeltérést
vagy kémiai SOH-t nem állapítunk meg. Nincs kényszerített mélykisütés vagy
töltési parancs. Az általános „akkumulátor élettartama még X év” állítás sem
következik ezekből az adatokból.

A módszerhez felhasznált elsődleges forrás: Baggu és mtsai.,
[Performance and Health Test Procedure for Grid Energy Storage Systems](https://docs.nlr.gov/docs/fy17osti/67419.pdf),
az energia/SOC alapú ellenőrzés és az azonos SOC-n záródó hatásfokmérés elve.
A fenti mintaküszöbök és UI-értékelések a projekt saját megvalósításai.

## Mentés és erőforrások

A diagnosztika a napi `/stats/YYYY-MM-DD.json` opcionális `battery_diagnostics`
objektumában, a meglévő tmp/bak, fsync és atomi mentési útvonalon tárolódik.
A szokásos mentési periódus érvényes, nem írunk minden 5 másodperces mintánál.
Áramszünetkor a legutóbbi sikeres mentés utáni adatok elveszhetnek. Az éjfél
átviszi az előzményeket; bootkor a legutóbbi elérhető, legfeljebb 35 napos napi
fájl referenciája tölthető vissza. Régi, diagnosztika nélküli statisztika érvényes
marad. Hibás diagnosztikai blokk nem teszi tönkre az energiaösszegzőket.

Félkész szakaszt és töltés/kisütés párosítási állapotot nem állítunk vissza.
Modulbővítéskor vagy névleges kapacitásváltáskor új diagnosztikai referencia és
megfigyelt ciklusszámláló indul; a korábbi normál napi energiamérések megmaradnak.

A két háttér **1280×800 RGB PNG**, egyenként legfeljebb 1,4 MiB, együtt
kevesebb mint 2,8 MiB. Az előnézetek **480×300** méretű valódi LVGL-képek.
A képek SD-erőforrások; nem növelik a beágyazott PNG-készletet. A modell
legfeljebb 1 KiB, a UI-jelentés legfeljebb 256 bájt; nincs új task vagy
mintánkénti memóriafoglalás. A napi állapotok és snapshotok fix másolatai ezt
a kis struktúrát is hordozzák.

Grafika: beépített imagegen, `photorealistic-natural` nappali kép és annak
`lighting-weather` éjszakai változata. Prompt: a megadott referenciához hasonló
fehér, egymásra épülő otthoni akkumodulok a ház külső falánál, kis betonlábon,
eresz alatt; természetes kert, fű, díszfüvek és fák; az akku a bal harmadban,
a mérőszámok a nyugodtabb jobb kétharmadban. Nappal természetes napfény; azonos
jelenet éjjel holdfénnyel és meleg ház-/kertvilágítással. Szöveg/logó nélkül.
Utómunka: 1280×800-ra méretezés, 256 színre kvantálás után RGB PNG kódolás.
[Képek és ellenőrzőösszegek](assets.md).

## Ellenőrzés

- A valódi C-modell teszteli a két energiairányt, SOC-ablakot, mediánt,
  illesztett és elutasított hatásfokpárokat, timeoutot, óraugrást, SOC-ugrást,
  kapacitásváltást, elavulást és JSON-visszatöltést.
- Az `app_stats` integráció tényleges SD-fájlokkal ellenőrzi az éjféli
  átmenetet, bootot és mind a négy helyi statisztikát számoló adatfogadási módot.
- A headless LVGL a firmware saját UI-ját rajzolja, a demóeredményt a valódi
  diagnosztikai modell állítja elő. Kevés adat és hiányzó referencia is tesztelt.
- A fizikai akkurendszer mérési pontját, előjelét és névleges/használható
  kapacitásának egyezését készülékes próbával kell ellenőrizni.

[Fejlesztési és folytatási állapot](battery-diagnostics-plan.md).
