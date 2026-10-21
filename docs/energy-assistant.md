# Helyi energiaasszisztens – v3 fejlesztési ág

Kizárólag Master/Loxone módban működik. A jelzés javaslat: sem a megjelenése,
sem az X nem küld kapcsolási parancsot. Nem használ felhőt vagy második Loxone-
kapcsolatot. A fogyasztók 12 kisméretű numerikus bejegyzése a meglévő WS-deltatárból
jön, nyitott fogyasztói oldal és betöltött ikonkészlet nélkül is.

## „Most érdemes…”

A Loxone webes fogyasztószerkesztőjében kapcsold be az adott fogyasztónál az
**Indítás javasolható** mezőt, és add meg az átlagos üzemi teljesítményét.
Alapból minden fogyasztónál kikapcsolt. A visszajelzett fogyasztásnak ismert,
valódi nullának kell lennie; a hiányzó minta nem nulla. Ez a konzervatív változat
még kis készenléti teljesítményből sem állítja biztosan, hogy a gép leállt.

Minden feltétel legalább 120 másodpercig teljesüljön:

- Friss, legfeljebb 10 másodperces validált energiaminta és élő Loxone-kapcsolat.
- Nappali, normál üzem; pontosan 100% SOC. Nincs állítható 80%-os töltési cél.
- Pozitív PV-termelés, hálózati export és nincs akkukisütés.
- Elég szabad többlet a géphez és `max(100 W, üzemi teljesítmény × 20%)` tartalékhoz.

Előjelek: hálózatnál negatív az export, akkunál pozitív a kisütés. A szabad
PV-többlet a mért export és a `PV − ház − akkutöltés` közül a kisebb érték.
Így a töltés elsőbbséget élvez, és az akkumulátorral megtámogatott export nem
lesz napelemes indításjavaslat. Egyszerre egy gép szerepel, a konfiguráció sorrendjében;
nem foglaljuk le ugyanazt a többletet több gép egyidejű indítására.

A jelzés a feltételek megszűnésekor azonnal eltűnik. Új kapcsolat, konfiguráció,
elavult/hiányzó minta vagy idő-visszalépés új minősítési időt igényel. X után
30 percig nincs új jelzés. Ugyanaz a gép csak akkor javasolható ismét, ha a feltételei
közben legalább 10 percig igazoltan nem teljesültek. Adathiány nem igazolja ezt.
A csendidő és bezárás RAM-állapot, reboot után nem áll helyre.

A tartósság a rendelkezésre álló mintákra vonatkozik: az energia a meglévő, kb.
5 másodperces frissítéssel, a fogyasztóérték a közös WS-deltával érkezik. Ez nem
villamos biztonsági vezérlés vagy hardveres folytonossági mérés.

## Magasabb éjszakai fogyasztás

A korábbi helyi 00:00–04:00 óraablakokból képzett, igazolt referencia szükséges:

- Az előző teljes helyi óra mind a 12 ötperces slotjában legalább 270, legfeljebb
  300 másodperc tényleges, éjszakainak jelzett, folytonos mérés kell.
- A referencia gyűjtéséhez és lekéréséhez szinkronizált óra szükséges.
- Lefedettség csak legfeljebb 30 másodperces, monoton idővel összhangban levő
  szomszédos mintapárokból keletkezik. Saját éjszakai energiaösszeg tartozik hozzá.
- Azonos helyi órára az előző hét legalább három érvényes éjszakája szükséges;
  ezek mediánja a referencia. A hiányzó nap kimarad, nem lesz nulla.
- A lezárt órás átlag és a jelenlegi élő fogyasztás egyaránt legalább 50%-kal
  **és** legalább 100 W-tal magasabb legyen. Az élő eltérés 60 másodpercig tartson.

A jelzés helyi 01:00–04:59 között, éjszakai állapotban jelenhet meg. A részletekben
az előző óra mért átlaga és a szokásos referencia látszik, nem két pillanatnyi érték.
X után azon a helyi éjszakán nem tér vissza. A feltételek megszűnésekor eltűnik.
Óra-visszalépés, nem egyező falióra/monoton idő és ismételt DST-slot kizárja az
érintett éjszakát. Nappali és túl ritka mérésből nem készül referencia.

A napfájl v3 formátuma opcionális `night_seconds` és `night_load_kwh` slotmezőkkel
bővül (csak az első 48 slotban). A régi fájl teljes energiaadata megmarad, de
igazolt éjszakai lefedettség nélkül nem használható ehhez a funkcióhoz.
Ezért frissítés után legalább három megfelelő új éjszaka szükséges.
A 301 másodperc invalid/ismételt ablakot jelöl. Hibás opcionális mező szigorúan
elutasítandó, ugyanúgy, mint a napfájl többi hibás adata.

A kis, hétnapos RAM-referencia a már létező statisztikai betöltés és napváltás
során készül. A UI nem olvas SD-t, nem tart új teljes napmásolatot és nem ír NVS-t.
A régi statisztikai összehasonlítások és a BLE 1005 wire-formátuma változatlan.

## Megjelenítés és ellenőrzés

A gyári témákban a javaslat a dekoratív címsáv helyére kerül, önálló kontrasztos
panellel és 44 pixeles X érintési területtel. Óra, időjárás, számérték, diagram és
házérintési terület szabadon marad. A Rendszer téma alapelrendezése változatlan.
Otthon/fogyasztói oldalon, diagnosztikán, betöltéskor és PIN-pad mellett rejtett;
a tartóssági modell közben minimális állapottal tovább lép. Slave/MQTT/HTTP módban
nincs jelzés. HU/EN felirat, 1 kW alatt W, felette egy tizedes kW jelenik meg.
Külső téma csak a szerző által megadott `assistant_area` területre rajzolhat;
lásd [témaszerzői leírás](template-authoring.md).

A modell heap és flash nélkül fut; natív állapota 440 bájt, a 12 fogyasztó
pillanatképe 1744 bájt. A napi adatszerkezet bővülése 480 bájt/nap, két RAM-napnál
960 bájt; a hétnapos referenciacache legfeljebb 512 bájt. Ezek szerkezeti keretek,
nem fizikai DMA/PSRAM/FPS-mérések. A modell és a bezárás ugyanazon LVGL-lock alatt
fut; a szolgáltatás-pillanatképek a grafikus lock előtt készülnek.

- `tests/test_energy_assistant.c`: tényleges szabálymodell UBSan-nal, előjelek,
  tartalék, invalid/stale/reconnect/revízió/idő, bezárás és éjszakai jelzés.
- `tests/test_core.c`: tényleges éjszakai integráció, SD-mentés, reboot, medián,
  régi/hiányos adatok, nem szinkronizált óra és idő-visszalépés.
- `tests/test_loxone_protocol.c`: tényleges WS-frissítőből érkező minimális
  asszisztens-pillanatkép, valódi nulla és disconnect invalidálás.
- `tests/preview/preview.c`: tényleges LVGL a Rendszer és mind a kilenc gyári
  téma nappali/éjszakai változatán, X, rejtett oldalak, widgetmegőrzés,
  fontos adatdobozokkal való átfedés kizárása. A bemenetek szintetikusak.

Fizikai Miniserver/panel/DMA/PSRAM/FPS próba továbbra is szükséges.

## English overview

The assistant is available on the Master in Loxone mode only. It never switches
a device. Solar advice requires explicitly enabled consumers with a known running
power and a measured zero load, exactly 100% battery SOC, no battery discharge,
and two minutes of sufficient PV export including a 20% / 100 W reserve.
Dismissal silences advice for 30 minutes; the same consumer also needs a new episode.

Overnight notices compare the last complete local hour against the median of at
least three adequately measured matching hours from the previous seven nights.
Both the hourly average and the live load must exceed that baseline by 50% and
100 W; the live condition must persist for a minute. Historical files without
measured coverage remain valid energy history but cannot establish this baseline.
The notice is hidden on controls, diagnostics and the PIN pad. Custom themes need
an explicit safe `assistant_area`; physical hardware validation is still pending.
