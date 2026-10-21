# v3 Loxone otthonkonfiguráció – fejlesztői szerződés

## Megvalósítási állapot

Az `app_loxone_home_model.c` platformfüggetlen konfigurációs modell elkészült.
A tartós SD-mentés és a meglévő `/loxone` oldal külön fogyasztói/Otthon-szerkesztője
is elkészült. Az élő értékek, a korlátos parancsküldés és a két Master kijelzőoldal is be van
kötve, külön SD-erőforráscsomaggal. A Slave-jelenlét, az energiaasszisztens és
a jóváhagyott 05/06 témák is implementálva vannak. A v3 fizikai eszközpróbája
és végső kiadási ellenőrzése még hátravan. A teljes feladat és a folytatás:
[fejlesztési terv](v3-development-plan.md), [telepítés és átadás](v3-handover.md).

A modell külön objektum; nem növeli meg a több helyen másolt, jelszót is tartalmazó
`lx_config_t` hitelesítési konfigurációt. A modellben nincs token vagy jelszó.
A készüléken csak Master és közvetlen Loxone módban érhető el.

## Tárolás és webes szerkesztés

- Külön `/sdcard/loxone-home.json`, legfeljebb 128 KiB, schema 2; jelszó és token nélkül.
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
- A szerkesztő 64 ikont és helyiség-/funkciószűrőt ad; a háttérfrissítés nem írja
  felül a helyi szerkesztést. A vezérlési cél listája a `control_uuid` mezőből
  készül, nem a mérési UUID-ból. A parancstámogatást a runtime is ellenőrzi: Switch visszajelzésnél az objektum
  saját `active` állapota szükséges; pulse célként Switch/Pushbutton/TimedSwitch
  támogatott. Védett és más típusú objektum tiltott; ilyen funkcióhoz Loxone-ban
  megfelelő virtuális nyomógomb/kapcsoló párosítható.

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

Az UUID-k szemléltetők, nem egy telepítés valós vezérlői. Az ikonkulcsok az `assets/loxone-home/catalog.json` készlethez tartoznak (a fenti
példakulcsok szemléltetők); a modell még csak a kulcs biztonságos
alakját vizsgálja, a tényleges fájlok meglétét a csomagkezelő fogja ellenőrizni.

## Korlátok és hibák

- Legfeljebb 12 fogyasztó és 8 elnevezhető Otthon-oldal, oldalanként legfeljebb 12 kártya.
- A schema 1 `cards` tömbje automatikusan egy `home` azonosítójú, „Otthon” című oldalra kerül. A mentés már schema 2 formátumú; az üres oldal is érvényes.
- A `pages[].title` legfeljebb 80 UTF-8 bájt, szabad szöveg; nem HTML. A szerkesztő a sorrendet is módosíthatja.
- Az `id` és `icon` 1–32 kisbetű, szám, kötőjel vagy aláhúzás; nincs fájlútvonal.
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

A `tools/prepare_home_resources.py` a forrásképekből és az ellenőrzött 64 ikonból
`dist/templates/loxone-home.zip` fájlt készít. Ez a meglévő `/templates` felületen
ellenőrizhető, telepíthető és frissíthető. Külön erőforrásként jelenik meg; nem
lapozható energiaelrendezés, és nem változtatja meg a mentett témaválasztást.

A ház megérintése a fogyasztókat, lefelé húzás az Otthon-kártyákat nyitja meg.
Ehhez Master Loxone mód, működő SD, érvényes közös csomag és konfigurált fogyasztó / legalább egy Otthon-oldal kell.
A jobb felső X pontosan az előző témát állítja vissza. Speciális oldalon a
függőleges/diagnosztikagesztus nem indít másik műveletet. Otthon nézetben a jobbra/balra húzás a saját oldalai között lapoz, körbefordulással; a témaválasztás változatlan. Több oldalnál alul pontok mutatják az aktív lapot, a fejlécben annak saját címe látható. Az egyes oldalak képei
csak aktív állapotuk alatt maradnak betöltve.

A pillanatnyi gomb egyszer küld; a kérést kapcsolatvesztés után nem ismételjük.
A kapcsoló a visszaérkező értéket mutatja, nyugta vagy érintés nem változtatja meg
optimistán. Friss kapcsolat, támogatott párosítás és érvényes kapcsolóállapot nélkül
a vezérlés tiltott. 2 másodpercnél régebbi sorba állított kérés nem hajtható végre.

A telepítés/tesztelés teljes v3 fejlesztési ágra vonatkozik; v3 release még nincs.


## Többoldalas memória- és képszerződés

A natív profil 32 112 bájt; a parser és az SD-s validálás heapen dolgozik, a két
UI-pillanatkép PSRAM-ban él. A legfeljebb 96 kártya és 12 fogyasztó értéktára
1824 bájt; a rejtett lapok delta állapotai sem vesznek el. Az aktív lap képei
legfeljebb egy hátteret, egy logót és 12 külön ikont jelentenek, nem 8 teljes UI-t.

A v2 közös erőforráscsomagban 64 ikon, 4×1280×800 háttér és logó van.
A fogyasztók `consumers-light/dark.png`, az Otthon-lapok `background-light/dark.png`
fájlokat használnak. A világos/sötét lábléc fedő panelje tartja a kontrasztot.
A teljes csomag dekódolt összes képe legfeljebb 20 MiB lehet (csak speciális
erőforráscsomagnál); normál témáknál a 12 MiB korlát megmarad. Egyszerre továbbra
is csak az aktív oldal képei töltődnek be, a 10 MiB LVGL-cache nem nőtt.
Régi v1 erőforráscsomag betölthető marad közös háttérrel; a külön fogyasztói
háttérhez a v2 `loxone-home.zip` SD-csomag is frissítendő.
A v3 csomag a webes sablonkezelőhöz 400×250-es nappali/éjszakai előnézetet is
ad. Ezek nem kerülnek az aktív LVGL-képek közé. A webes szerkesztő telepített
csomagnál megmutatja a kiválasztott ikon képét, nélküle felül pirosan figyelmeztet.
