# v3 Loxone otthonkonfiguráció – fejlesztői szerződés

## Megvalósítási állapot

Az `app_loxone_home_model.c` platformfüggetlen konfigurációs modell elkészült.
A tartós SD-mentés és a meglévő `/loxone` oldal külön fogyasztói/Otthon-szerkesztője
is elkészült. Az élő értékek, a korlátos parancsküldés és a két Master kijelzőoldal is be van
kötve, külön SD-erőforráscsomaggal. A Slave-jelenlét, az energiaasszisztens és
a jóváhagyott 05/06 témák is implementálva vannak. A v3 eredeti bevezetési
feladatait és ellenőrzési szempontjait a történeti
[fejlesztési terv](v3-development-plan.md), valamint a
[telepítési és átadási jegyzet](v3-handover.md) őrzi.
Az aktuális világítás-, IRC-, árnyékolás-, további vezérlő- és Audio-kártyák
[használati leírásai és képei](features/README.md#otthon-kártyák-leírásai-és-képei)
a funkciók dokumentációjából érhetők el.

A modell külön objektum; nem növeli meg a több helyen másolt, jelszót is tartalmazó
`lx_config_t` hitelesítési konfigurációt. A nyilvános JSON-ban és a kijelzőnek
átadott pillanatképben nincs token vagy jelszó. A kaputelefon kamera-jelszava
csak a belső futásidejű profilban nyílt szöveg; az SD-re titkosítva kerül
([tárolási szerződés](intercom.md#hálózat-és-hitelesítés)).
A készüléken csak Master és közvetlen Loxone módban érhető el.

## Energiaáramlás nélküli használat

Az [Energiaáramlás tiltás](energy-flow-disable.md) az Otthon-kártyákat alapnézetté
teszi. A fogyasztók ilyenkor rejtettek, az üres Otthon helyett logó és 10% fényerő
jelenik meg; a kaputelefon önállóan használható. A webes fogyasztói, Otthon- és
kaputelefon-szakasz normál módban is külön menthető.

A `/api/loxone/home` új kérései `schema: 2` mellett `section` mezőben a
`consumers`, `pages` vagy `intercom` értéket és kizárólag a megadott szakasz
adatát küldik. A szerver a legfrissebb mentett profil többi szakaszát megőrzi,
revízióváltás vagy párhuzamos mentés esetén újrapróbálkozást kér. Normál módban
a régi teljesprofil-kérés továbbra is támogatott; tiltáskor teljesprofil- és
fogyasztói írás elutasított. A kamera-jelszó megőrzése szerveroldali.

## Tárolás és webes szerkesztés

- Külön `/sdcard/loxone-home.json`, legfeljebb 128 KiB, schema 2; nyílt jelszó és token nélkül.
- Ideiglenes `.tmp`, sikeres flush/fsync után FAT-kompatibilis `.bak` csere.
  Induláskor az első érvényes fő-/backup-profil töltődik be. Hibás fájl nem
  módosítja a memóriabeli kimenetet. A túl mély JSON még a parser előtt elutasított.
- A titkosított `loxone.json` kapcsolatadatok külön maradnak; a régi kapcsolat
  mentése nem törli a kártyákat, és a kártyák mentése nem módosít jelszót.
- A közös webes hitelesítés mögött `GET /api/loxone/home` adja a profilt,
  `busy`, `revision` és `status` mezővel. `GET` schema 2 profilt ad; `POST` ugyanide schema 1/2 profilt fogad,
  `X-Zolee-Loxone: 1` fejléccel, legfeljebb 128 KiB törzzsel.
- A 202 csak sorba állítást jelez. Sikeres háttérmentéskor a revízió növekszik,
  hiba esetén a korábbi profil marad és a `status` közli az SD-hibát.
- A mentés a meglévő Loxone-háttérfeladatban fut. Jelenleg egy élő kapcsolatot
  újraépít, ezért mentés után rövid adatkapcsolati szünet lehetséges.
- A szerkesztő az Otthon-csomag ikonkatalógusát és helyiség-/funkciószűrőt ad; a háttérfrissítés nem írja
  felül a helyi szerkesztést. A vezérlési cél listája a `control_uuid` mezőből
  készül, nem a mérési UUID-ból. A parancstámogatást a runtime is ellenőrzi: Switch visszajelzésnél az objektum
  saját `active` állapota szükséges; pulse célként Switch/Pushbutton/TimedSwitch
  támogatott. Általános kapcsoló-/impulzusparancshoz védett és más típusú objektum tiltott; ilyen funkcióhoz Loxone-ban
  megfelelő virtuális nyomógomb/kapcsoló párosítható.
- A [világításvezérlő](lighting-controller.md) és az [IRC termosztát](irc-controller.md)
  saját blokktípust és natív parancsokat használ. IRC-nél csak az
  `IRoomControllerV2` szülőblokk választható; a `thermostat-irc` ikon rögzített,
  nincs ikonválasztó vagy külön visszajelzőállapot-párosítás.
- A [Redőnyvezérlő](shading-controller.md) a `CentralJalousie` és az automatikus
  `Jalousie` szülőblokkot kezeli. A `blinds-shading` ikon rögzített; a kijelzőn
  `Fel / Stop / Le` gombok vannak, egyedi blokknál külön százalékos ablakkal.
  A weboldal a konfigurációt szerkeszti; Auto gombot nem ad.

A fogyasztói képernyő címe „Ház külön mért fogyasztói”. Az érvényes,
1 W-nál kisebb készülékfogyasztás 0 W-ként és szürke, kikapcsolt ikonként
jelenik meg; pontosan 1 W már aktív. A fejléc „A ház teljes fogyasztása”
felirat alatt a friss PowerFull értéket mutatja, W/kW formázással. Hiányzó,
elavult vagy megszakadt adatkapcsolatnál „Nincs adat” látszik. Az egyedi
fogyasztók hiányzó értékei nem helyettesítik a teljes ház mérését.

### Otthon-oldalak fülei

Az Otthon-kártyák szerkesztőjében minden oldal külön fülön választható.
A fül az oldal nevét és a kártyák számát mutatja; az aktív panelen található
az oldalnév, az **Oldal előrébb / Oldal hátrébb** rendezés, az oldal és
kártyáinak törlése, valamint a kártyák szerkesztője. Az oldalnév gépelés
közben frissíti a fül feliratát, a mező és a kurzor megmarad.

Az utolsó **+** fül új oldalt hoz létre, azt rögtön kiválasztja, és a
névmezőre helyezi a fókuszt. Nyolc oldalnál a hozzáadás eltűnik, törlés után
ismét elérhető. Üres Otthonnál a + gomb indítja az első oldal létrehozását.
A kártyánkénti típusválasztás és fel/le mozgatás az aktuális oldalon működik;
az oldalátrendezés a teljes oldalt mozgatja a kártyáival együtt.

Mobilon a fülsáv oldalra görgethető, az aktív fül kiválasztáskor látható
területre kerül. A füleken a bal/jobb nyíl, valamint a Home/End billentyű
is oldalt vált. Oldalváltás és rendezés megtartja a nem mentett mezőket.
Az **Otthon-beállítások mentése** valamennyi oldal módosítását menti;
a fülváltás önmagában nem ment. A mentés és a tartós írásra várakozás
közben a fülek és a szerkesztő zároltak, a háttérfrissítés továbbra sem
írja felül a helyi módosításokat.

## Formátum

A [letölthető mintaprofil](../examples/loxone-home.sample.json) a valódi
C-parserrel és mentés/visszatöltéssel ellenőrzött, létező ikonkulcsokat használ.
Az indításjavaslat alapból kikapcsolt. Az UUID-k szemléltetők: a saját Loxone-
állapotokat és vezérlőket a `/loxone` szerkesztőben válaszd ki. Ez a fájl
nem a hálózati `config.json`, és a webes szerkesztőnek nincs JSON-import gombja.
Kézi SD-s használatnál a kitöltött fájl neve `loxone-home.json`; a módosítás
után újraindítás szükséges. A webes mentés a javasolt út.

```json
{
  "schema": 2,
  "consumers": [
    {
      "id": "dishwasher",
      "name": "Mosogatógép",
      "icon": "dishwasher",
      "source": {
        "uuid": "11223344-5566-7788-99aabbccddeeff00",
        "scale": 0.001
      },
      "average_kw": 1.4,
      "suggest_start": false
    }
  ],
  "pages": [
    {
      "id": "ground-floor",
      "title": "Földszint",
      "cards": [
        {
          "id": "living-temperature",
          "name": "Nappali hőmérséklet",
          "icon": "temperature",
          "type": "value",
          "source": {
            "uuid": "11223344-5566-7788-99aabbccddeeff01",
            "scale": 1,
            "offset": 0
          },
          "unit": "°C",
          "decimals": 1,
          "warning_low": 18,
          "warning_high": 28,
          "warning_color": "#E66B6B"
        },
        {
          "id": "water-boost",
          "name": "Melegvíz-rásegítés",
          "icon": "water-heater",
          "type": "pulse",
          "control_uuid": "aabbccdd-5566-7788-99aabbccddeeff00"
        },
        {
          "id": "garden-light",
          "name": "Kerti világítás",
          "icon": "lamp",
          "type": "switch",
          "control_uuid": "aabbccdd-5566-7788-99aabbccddeeff01",
          "source": {
            "uuid": "11223344-5566-7788-99aabbccddeeff02",
            "scale": 1
          }
        }
      ]
    },
    {
      "id": "garden",
      "title": "Kert",
      "cards": []
    }
  ]
}
```

Az UUID-k szemléltetők, nem egy telepítés valós vezérlői. Az általános
ikonkulcsok az `assets/loxone-home/catalog.json` készlethez tartoznak; a modell
a kulcs biztonságos alakját vizsgálja, a tényleges fájlokat a csomagkezelő
ellenőrzi. Kivétel az `irc` kártyatípus: annak `icon` mezője mindig
`thermostat-irc`, a parser és a szerializáló is erre normalizálja, hibás vagy
hiányzó importált ikon esetén is. Az ikon a v4 Otthon SD-csomag része, nem a firmware-be ágyazott kép. Az IRC mentett mezői: `id`, `name`, `type`, `icon`, `control_uuid`;
nincs `source` vagy általános értékformázás. [IRC-profilpélda és protokoll](irc-controller.md#konfiguráció-és-protokoll).

A `shading` kártyatípus ugyanezt az öt mezőt menti, rögzített
`blinds-shading` ikonnal. A parser, a szerializáló és a szerkesztő a hiányzó
vagy hibás importált ikont is normalizálja, a rejtett oldalak kártyáinál is.
A központi/egyedi működést a kiválasztott blokk metaadata határozza meg,
nem egy második konfigurációs kapcsoló.
[Redőny-profilpélda és protokoll](shading-controller.md#konfiguráció-és-protokoll).

## További Loxone-blokkok

Az alábbi nyolc típusnál a szülőblokk `control_uuid` azonosítóját kell
kiválasztani. Külön visszajelző UUID, skálázás vagy általános értékformázás
nem kerül a profilba; ezek a Loxone-struktúra metaadataiból következnek.
A szerkesztő a kiválasztott típushoz tartozó katalógusbejegyzéseket kínálja.
A csak olvasható Ajtó–ablak összesítőhöz parancscím sem szükséges.

| Kártyatípus | JSON `type` | Loxone-blokk | Ikon |
|---|---|---|---|
| Önálló dimmer | `dimmer` | `Dimmer` | Választható; alapként `dimmer` |
| Központi világítás | `central-lighting` | `CentralLightController` | Kötött `central-lighting` |
| Analóg értékállító – Slider | `slider` | `Slider` | Választható; alapként `slider` |
| Részletes időzített kapcsoló | `timed-switch` | `TimedSwitch` | Választható; alapként `timed-switch` |
| Ajtó–ablak összesítő | `window-monitor` | `WindowMonitor` | Kötött `window-monitor` |
| Kapu / garázsajtó | `gate` | `Gate` | A Loxone kaputípusából automatikus |
| Motoros ablak | `window` | `Window` | Kötött `motor-window` |
| Audio | `audio` | `AudioZoneV2` | Kötött `audio-player`, SD-lejátszóképpel |

A hét általános új kártya kezelése és valódi LVGL-képei a
[Loxone-kártyák útmutatójában](loxone-home-controls.md), a nyolcadik típusé
a külön [Audio-útmutatóban](audio-controller.md) szerepelnek. A
[közös képmutató](features/README.md#otthon-kártyák-leírásai-és-képei)
minden blokkhoz közvetlen képhivatkozást ad.

Ezek a kártyák az `id`, `name`, `type`, `icon`, `control_uuid` mezőket mentik.
Az időzített kapcsoló további `allow_permanent` logikai mezője alapból `true`.
A **Tartós bekapcsolás engedélyezése** jelölés kikapcsolásával az adott
kártyán nem indítható tartós üzem; a Ki művelet ettől függetlenül elérhető.
Az időtartamot a Loxone-blokk határozza meg, a kijelzőn nincs külön időtartamlista.

A kapu képe az aktuális `gate_animation` katalógusértékből következik:

| Érték | Megjelenített típus | SD-ikon |
|---|---|---|
| 0 | Garázskapu | `gate-garage` |
| 1 | Balra nyíló kapu | `gate-left` |
| 2 | Jobbra nyíló kapu | `gate-right` |
| 3 | Dupla kapu | `gate-double` |
| 4 | Balra nyíló tolóajtó | `gate-slide-left` |
| 5 | Jobbra nyíló tolóajtó | `gate-slide-right` |
| Ismeretlen | Kapu | `gate` |

Csak egész, numerikus 0–5 érték jelent ismert típust. A kártyanév és az
importált ikon nem írja felül a blokk típusát. A firmware a mentett `gate`
ikont semleges azonosítóra normalizálja; a konkrét képet mindig az aktuális
katalógus alapján választja. A webes szerkesztő külön kiírja a felismert típust.

Az ikonok, az IRC-termosztát és a redőnyvezérlő képe is az Otthon SD-csomagból
érkezik. A szerkesztő a telepített csomag verziójával kéri az előnézetet,
így csomagfrissítés után nem marad a böngészőben a régi kép. Hiányzó vagy
v1–v3 csomagnál az új képek helyén frissítési útmutatás jelenik meg; ez nem
akadályozza a konfiguráció mentését. A csomag neve `loxone-home.zip`.
Az ikonnevek csoportos előtagot kapnak, például „Lámpa - mennyezeti” és
„Lámpa - oldalfali”; a mentett stabil ikonazonosítók megmaradnak.

## v4 Otthon-erőforráscsomag frissítése

Az új firmware telepítése után frissítsd a Sablonok oldalon a hozzá tartozó
`loxone-home.zip` csomagot. A v4 csomag 79 különálló 96×96 ikont, két
292×200 Audio-lejátszóképet, négy hátteret, két előnézetet, logót és
manifesztet tartalmaz, összesen 89 ZIP-bejegyzéssel. A PNG-k a firmware
helyett az SD-re kerülnek; az IRC és a redőny jóváhagyott képe is itt van.

A v1–v3 csomagok az új firmware-rel továbbra is betölthetők. A még nem
szereplő új ikon helyén a kártya szöveges megjelenítése működik; a régi
redőnyikon felhasználható, az IRC nem kap eltérő geometriájú helyettesítő
képet. A manifesztben deklarált, de hiányzó vagy sérült fájl továbbra is hiba.
A webes szerkesztő a hiányzó új képnél frissítési útmutatást ad, a mentést
ettől függetlenül engedi.

Visszaállításkor a firmware és a csomag összetartozó verzióját használd:
az új firmware mellett előbb állítsd vissza a v1–v3 csomagot, utána a régi
firmware-t és szükség szerint az előző konfigurációmentést. A korábbi
firmware a v4 csomagot és az új kártyatípusokat nem kezeli.
[A csomag részletes szerződése és méretkeretei](../assets/loxone-home/README.md).

## A szerkesztő elrendezése

Minden fogyasztó és Otthon-kártya saját, világosabb hátterű kártyalapot kap,
a nevével címkézve. Az Otthon-kártyán a **Kártyatípus** az első, teljes
szélességű mező. Új kártyán „Válassz…” állapotból indul; a név, a blokk,
a megjelenítési mezők és az ikon csak típusválasztás után jelennek meg.
Típus nélküli kártya nem menthető.

A típus után a Loxone-blokk vagy visszajelző állapot, a név, a típusfüggő
beállítások, végül az ikonválasztó következik. A mezők billentyűzetes
fókuszsorrendje is ezt követi. A forrás kiválasztásakor az üres vagy korábbi
blokknevet használó név átveheti az új blokk nevét; az egyedi név megmarad.
Az ikon előnézete a részletek mellett látható, a színek és a kompakt rács
megmaradnak. Mobilon a mezők egy oszlopba rendeződnek.

A kártya **bal alsó sarkában a Fel / Le gombok** az aktuális Otthon-oldalon
belül, egy hellyel mozgatják a teljes kártyát. Az első kártyán a Fel,
az utolsón a Le inaktív; egyetlen kártyán mindkettő. A törlés jobb alul marad.
A mozgatás megőrzi az azonosítót, a nem mentett mezőket és a billentyűzetes
fókuszt. A sorrend az oldal `cards` tömbjében tárolódik, külön sorszám nélkül;
a szokásos Otthon-mentés után a kijelző is ezt követi. Oldalak közötti
mozgatást ezek a gombok nem végeznek; az oldalak saját rendezőgombjai megmaradnak.

Csak a kiválasztott kártyatípushoz szükséges mezők jelennek meg. IRC-nél és
redőnynél a típus, szülőblokk és név mellett a kötött ikon előnézete látható;
a skálázási, mértékegység-, tizedesjegy- és figyelmeztetési mezők rejtettek.
Általános kártyatípusra váltva az ikonválasztás visszatér, az inkompatibilis
vezérlési cél törlődik.

## Korlátok és hibák

- Legfeljebb 12 fogyasztó és 8 elnevezhető Otthon-oldal, oldalanként legfeljebb 12 kártya.
- A schema 1 `cards` tömbje automatikusan egy `home` azonosítójú, „Otthon” című oldalra kerül. A mentés már schema 2 formátumú; az üres oldal is érvényes.
- A `pages[].title` legfeljebb 80 UTF-8 bájt, szabad szöveg; nem HTML. A szerkesztő a sorrendet is módosíthatja.
- Az `id` és az általános `icon` 1–32 kisbetű, szám, kötőjel vagy aláhúzás; nincs fájlútvonal.
  Az IRC-kártya ikonja minden esetben `thermostat-irc`, a redőnyé `blinds-shading`.
- Az oldalak azonosítója egyedi; a kártyaazonosító az összes oldalon együtt egyedi (oldalváltás közben sem változhat meg a parancscél). A fogyasztók külön névtérben élnek. A név legfeljebb 80 UTF-8 bájt,
  vezérlőkarakterek nélkül. A mértékegység legfeljebb 16 UTF-8 bájt.
- A Loxone UUID 35 karakteres gyártói formátum, nem szabványos kötőjelezésű UUID.
- A `source.scale` véges, nem nulla, abszolút értéke legfeljebb 1 000 000.
  Az elhagyható `offset` alapértéke nulla, szintén véges és ilyen korlátú.
- Fogyasztónál az eltolás nulla; a skálázott eredmény kW.
- Az `average_kw` hiányozhat vagy `null`; megadva pozitív, legfeljebb 1000 kW.
  Ez konfigurációs hibatűrési korlát, nem módosítja a meglévő energiaminta határait.
- A `suggest_start` alapból hamis. Hiányzó átlagteljesítménnyel bekapcsolt jelölés
  mellett sem adunk kitalált teljesítményigényt az asszisztensnek.
- Értékkártyán 0–3 tizedes választható, alapból 1. Figyelmeztetési határok
  opcionálisak; mindkettőnél az alsó nem lehet nagyobb a felsőnél. A szín `#RRGGBB`.
- Pulse esetén vezérlési cél kötelező, állapotforrás nem kell. Kapcsolónál mindkettő
  kötelező; a visszajelzéshez skálázás 1 és eltolás 0 szükséges.
- Világításvezérlő, IRC és redőny esetén a szülőblokk `control_uuid` mezője kötelező,
  külön `source` nem kell. Az IRC kizárólag natív `− / +`, 0,5 °C-os léptetést
  ad; Auto gomb vagy saját szabályozási időprogram nincs.
- A redőny `FullUp`, `stop` és `FullDown` parancsot használ. Egyedi automatikus
  blokknál a külön ablak 0–100%-os pozíciót kér `manualPosition` paranccsal;
  központi blokknál nincs százalékos állítás. A skála 0%-nál teljesen
  felhúzott, 100%-nál teljesen leengedett állapotot jelent.
- Hiányzó teljes profil (`NULL`) régi telepítésnél üres konfiguráció.
- Hibás profil, túl sok elem vagy foglalási hiba esetén a korábbi kimenet változatlan.
  A hiba pl. `consumers[2].duplicate_id` alakban mutatja az érintett mezőt.

A hálózati struktúrából készülő katalógus az állapot `uuid` mellett stabil
`control_uuid` mezőt is tartalmaz. Az eltérő tényleges parancscím az opcionális
`action_uuid`, a LoxAPP3 `uuidAction` mezőjéből. A mentett profil csak a stabil
azonosítót tartja; a parancs a friss katalógusból feloldott címre megy.
Ha a `uuidAction` hiányzik, a korábbi érvényes objektumkulcs használható;
explicit hibás címnél vezérlés nem engedélyezett, az állapot továbbra is olvasható.

## Elfogadott fogyasztói elrendezés

A 1280×800 Master-oldalon legfeljebb négy kártya van egy sorban, legfeljebb
három sorban. A kártyák mérete minden elemszámnál azonos. A hiányos utolsó sor
vízszintesen középre igazított, a két szélén azonos üres hellyel.
Hét elemnél négy kártya van az első sorban és három középre igazítva a másodikban.
Öt elemnél az utolsó egy kártya, hatnál az utolsó kettő kerül középre.

A közös geometria a `app_loxone_home_layout.h` `lx_home_card_rect()` függvénye.
A tényleges LVGL-oldal ezt hívja; a megjelenítőben ne legyen ettől eltérő másolt képlet.
Nulla vagy 12 feletti elemszám, hibás index és hiányzó kimenet elutasított.
A natív LVGL-render a hétkártyás eset tényleges pozícióját is ellenőrzi.

## Ellenőrzés

```sh
python3 tests/test_loxone_home.py
python3 tests/test_loxone_home_store.py
node tests/test_loxone_home_web.js
```

A valódi C-modulokkal futó teszt oda-vissza JSON-kezelést, elemszámhatárt,
hibás párosítást, nem véges számokat, opcionális átlagteljesítményt, külön
parancs-/állapot-UUID-t és minden JSON-szerializálási foglalási pont hibáját vizsgálja.
A teszt UBSan és 8192 bájtos stack-frame fordítási korlát mellett fut.
Ez nem paneles RAM-/FPS-mérés és nem Miniserver-parancsküldési próba.


## Kijelzőoldalak és közös csomag

A `tools/prepare_home_resources.py` a forrásképekből, az ellenőrzött 79 ikonból
és a két Audio-lejátszóképből a v4
`dist/templates/loxone-home.zip` fájlt készít. Ez a meglévő `/templates` felületen
ellenőrizhető, telepíthető és frissíthető. Külön erőforrásként jelenik meg; nem
lapozható energiaelrendezés, és nem változtatja meg a mentett témaválasztást.

Az IRC termosztátikon és a kötött redőnyikon is az SD-csomag része;
a firmware nem tartalmaz belőlük beágyazott példányt. Az új képekhez a v4
csomag szükséges. Régebbi, érvényes csomagnál az IRC szöveges megjelenítést,
a redőny a korábbi, azonos tartalmú `blinds` képet használhatja.

A ház megérintése a fogyasztókat, lefelé húzás az Otthon-kártyákat nyitja meg.
Ehhez Master Loxone mód, működő SD, érvényes közös csomag és konfigurált fogyasztó / legalább egy Otthon-oldal kell.
A jobb felső X pontosan az előző témát állítja vissza. Speciális oldalon a
függőleges/diagnosztikagesztus nem indít másik műveletet. Otthon nézetben a jobbra/balra húzás a saját oldalai között lapoz, körbefordulással; a témaválasztás változatlan. Több oldalnál alul pontok mutatják az aktív lapot, a fejlécben annak saját címe látható. Az egyes oldalak képei
csak aktív állapotuk alatt maradnak betöltve. Kártyalapozáskor a háttér és a logó
már dekódolt puffere megmarad (a háttér csak a nézettől, a napszaktól és a
csomagtól függ), csak az új lap ikonjai töltődnek be. Napszak-, nézet- vagy
csomagváltásnál és bármilyen betöltési hibánál a teljes oldal újratöltődik.

A pillanatnyi gomb egyszer küld; a kérést kapcsolatvesztés után nem ismételjük.
A kapcsoló a visszaérkező értéket mutatja, nyugta vagy érintés nem változtatja meg
optimistán. Friss kapcsolat, támogatott párosítás és érvényes kapcsolóállapot nélkül
a vezérlés tiltott. 2 másodpercnél régebbi sorba állított kérés nem hajtható végre.

A fejlesztéskori v3 ellenőrzési naplót a [fejlesztési terv](v3-development-plan.md),
az aktuális firmware telepítését az [USB útmutató](usb-install.md) ismerteti.


## Többoldalas memória- és képszerződés

A natív gazdagépes mérésben a profil 33 776 bájt; a parser és az SD-s validálás
heapen dolgozik. A két konfigurációs UI-pillanatkép (`home_config`,
`home_next_config`) és két értékpillanatkép (`home_values`,
`home_next_values`) statikus `TEMPLATE_RAM` terület: a firmware-ben PSRAM BSS,
nem bezáráskor visszaadható heapfoglalás. A legfeljebb 96 kártya és 12 fogyasztó
egy értékpillanatképe a gazdagépen 1824 bájt. A rejtett lapok utolsó állapotait a konfigurált UUID-kre szűrt,
közös nyers állapottár őrzi; világítási, IRC-, redőny- és további vezérlőmodell csak a látható
oldalhoz él. Lapváltáskor a modell ebből a tárból épül fel, új
állapotfeliratkozás nélkül. [Állapottár és korlátai](lighting-controller.md#memória-és-láthatóság).
Az aktív lap képei
legfeljebb egy hátteret, egy logót és 12 külön ikont, valamint Audio-kártyánál
az aktuális nappali vagy éjszakai lejátszóképet jelentik. Az Audio borítóképei
külön, korlátos memóriatárolóban élnek; [részletek](audio-controller.md#memória-és-életciklus).
Az azonos oldali IRC-kártyák egyetlen közös, 96×96-os SD-képet
használnak; annak tartós ARGB8888-képpontadata 36 864 bájt, lapváltáskor és
bezáráskor felszabadul. [IRC-memóriakezelés](irc-controller.md#memória-és-láthatóság).
Az aktív oldal redőnykártyái ugyanígy egyetlen közös, 96×96-os SD-
képet használnak, 36 864 bájtos tartós képpontpufferrel. A két fix ikontípus
saját közös pufferét csak akkor foglalja le, ha szerepel a látható oldalon.
[Redőny-memóriakezelés](shading-controller.md#memória-és-láthatóság).

A korábbi **v2 csomag** 64 ikont, 4×1280×800 hátteret és logót tartalmazott.
A **v3 csomag** ezt két webes előnézettel egészítette ki; a jelenlegi
**v4 csomag** 79 ikonra és két Audio-lejátszóképre bővült. A manifeszt
v4-ben 1–96 ikont enged, a v1–v3 kompatibilitási ellenőrzése pontosan 64-et vár.
A fogyasztók `consumers-light/dark.png`, az Otthon-lapok `background-light/dark.png`
fájlokat használnak. A világos/sötét lábléc fedő panelje tartja a kontrasztot.
A teljes csomag dekódolt összes képe legfeljebb 20 MiB lehet (csak speciális
erőforráscsomagnál); normál témáknál a 12 MiB korlát megmarad. Egyszerre továbbra
is csak az aktív oldal képei töltődnek be, a 10 MiB LVGL-cache nem nőtt.
A Home-képek a kész oldal előtt, külön birtokolt pufferbe dekódolódnak:
RGB565 háttér és ARGB ikon/logó, előre kész szürke fogyasztói ikonokkal.
A Home-rajzolás már nem használ PNG-dekódolást; hiba esetén a teljes betöltés
visszavonódik, legfeljebb 15 újrapróbálás után visszatér a témára.
Az SD-zár foglaltsága várakozást jelent, nem fogyasztja a valódi hibák
újrapróbakeretét. A lapozáshoz átmenetileg megtartott háttér és logó is
felszabadul, ha a betöltés végül meghiúsul vagy az Otthon nézet bezárul.
Régi v1 erőforráscsomag betölthető marad közös háttérrel; a külön fogyasztói
háttér a v2-től érhető el. A v3-tól a webes sablonkezelő 400×250-es
nappali/éjszakai előnézetet is kap. Ezek nem kerülnek az aktív LVGL-képek közé.
A webes szerkesztő telepített
csomagnál megmutatja a kiválasztott ikon képét, nélküle felül pirosan figyelmeztet.
Az IRC és a redőny kötött ikonelőnézetét is a telepített SD-csomagból szolgálja
ki a hitelesített végpont; hiányzó képnél 404-et ad. A v4-re váltás és a régi
csomagok megjelenési tartalékai a [frissítési szakaszban](#v4-otthon-erőforráscsomag-frissítése) szerepelnek.

### Mi marad meg az Otthon nézet bezárása után?

A bezárás a látható oldalhoz tartozó dinamikus erőforrásokat elengedi.
A teljes alkalmazás szabad memóriája ettől nem tér vissza a Loxone
bekapcsolása előtti értékre: az adatkapcsolat és a többi funkció tovább működik.

| Erőforrás | Lapváltás vagy bezárás eredménye |
| --- | --- |
| Világítás-, IRC-, redőny- és a hét további vezérlő oldalmodellje; WindowMonitor-részletlista | Az előző oldal modellje azonnal leválik és felszabadul, függő parancsnyugta mellett is. Az új oldal saját modellje ezután épül fel. |
| Világítás részletes kezelője | A részletablak bezárása a részletmodellt engedi el; az oldal bezárása az oldalmodellt is. |
| Home-képek, PNG-források, dekódolt és szürke ikonpufferek | A bezárás vagy a végleges betöltési hiba mindet elengedi. Sikeres Otthon-lapozásnál a közös háttér és logó a következő oldalt is kiszolgálja. |
| Audio-oldal, kedvencek, borítóképek és natív kapcsolat | A modell és a kedvencek azonnal felszabadulnak; a kapcsolat és az aktív letöltés a biztonságos leállási pontján adja vissza a puffereit. [Részletes életciklus](audio-controller.md#bezáráskor-felszabaduló-és-megmaradó-állapot). |
| Még el nem küldött vezérlés | A Dimmer helyi összevont célja az oldalmodellel együtt megszűnik. A közös egyelemű Home/kaputelefon parancssor régi lapparancsa érvénytelenné válik, a worker küldés nélkül eldobja. Egy már elküldött parancs nyugtázása még befejeződhet. |
| Közös profil, értékek és nyers Miniserver-állapottár | Megmaradnak a fogyasztókhoz, energiaasszisztenshez, kaputelefonhoz és az oldal következő megnyitásához. A nyers tár legfeljebb 512 UUID-t és összesen 64 KiB szöveget tart, egy állapot legfeljebb 32 KiB lehet. A táblabejegyzések memóriája a szövegkereten felül értendő. |
| Statikus UI-pillanatképek, közös parancssor és szinkronizáció | Állandó szolgáltatásállapot; nem egy elrejtett oldal dinamikusan megtartott tartalma. A pillanatképek lefoglalt PSRAM BSS-területe bezárással nem szabadítható fel. |

A 2026. november 14-i ellenőrzésben a tényleges vezérlőszolgáltatás 96
nyitás/bezárás ciklusa pontosan a közös allokációs alapszintre tért vissza,
nyugtára várás közbeni bezárással is. A valódi LVGL-próba külön ellenőrzi
a nappali/éjszakai lapozás utáni hibakilépést és az SD-zárra várakozás
közbeni bezárást, RGB565 és 32 bites képmódban. Ezek gazdagépes
életciklus-próbák; a panel belső RAM-/DMA-mélypontját és a tényleges
váltási időt továbbra is a készülék diagnosztikája mutatja meg.

## Kompakt szerkesztő és autótöltés

A Fogyasztók és az Otthon-kártyák minden beállítása látható, tömör mezőrácsban,
külön nagy ikonelőnézettel. A meglévő színek, feliratok és zöld legendák megmaradnak.
Az ikonok magyar név szerinti ABC-sorrendet kapnak. Az `ev-charger` ikonnal jelölt
fogyasztók összegét az [Elektromos autó téma](electric-car.md) jeleníti meg.

A téma a mai töltési kWh-t és a közvetlen napelemes rész becsült arányát is
megjeleníti. A ház teljes fogyasztása továbbra is a `PowerFull`; az autótöltés
nem adódik hozzá még egyszer. [Statisztikai számítások](statistics.md#autótöltés-és-napelemes-arány).
