# v3 Loxone otthonkonfiguráció – fejlesztői szerződés

## Megvalósítási állapot

Az `app_loxone_home_model.c` platformfüggetlen konfigurációs modell elkészült.
A tartós SD-mentés és a meglévő `/loxone` oldal külön fogyasztói/Otthon-szerkesztője
is elkészült. **Az élő értékek, a parancsküldés és a két új kijelzőoldal még nincs
bekötve.** A szerkesztő már menthető profilt készít, ez még nem a teljes v3 funkció. A teljes feladat és a folytatás: [fejlesztési terv](v3-development-plan.md).

A modell külön objektum; nem növeli meg a több helyen másolt, jelszót is tartalmazó
`lx_config_t` hitelesítési konfigurációt. A modellben nincs token vagy jelszó.
A készüléken csak Master és közvetlen Loxone módban érhető el.

## Tárolás és webes szerkesztés

- Külön `/sdcard/loxone-home.json`, legfeljebb 32 KiB, schema 1; jelszó és token nélkül.
- Ideiglenes `.tmp`, sikeres flush/fsync után FAT-kompatibilis `.bak` csere.
  Induláskor az első érvényes fő-/backup-profil töltődik be. Hibás fájl nem
  módosítja a memóriabeli kimenetet. A túl mély JSON még a parser előtt elutasított.
- A titkosított `loxone.json` kapcsolatadatok külön maradnak; a régi kapcsolat
  mentése nem törli a kártyákat, és a kártyák mentése nem módosít jelszót.
- A közös webes hitelesítés mögött `GET /api/loxone/home` adja a profilt,
  `busy`, `revision` és `status` mezővel. `POST` ugyanide schema 1 profilt vár,
  `X-Zolee-Loxone: 1` fejléccel, legfeljebb 32 KiB törzzsel.
- A 202 csak sorba állítást jelez. Sikeres háttérmentéskor a revízió növekszik,
  hiba esetén a korábbi profil marad és a `status` közli az SD-hibát.
- A mentés a meglévő Loxone-háttérfeladatban fut. Jelenleg egy élő kapcsolatot
  újraépít, ezért mentés után rövid adatkapcsolati szünet lehetséges.
- A szerkesztő 64 ikont és helyiség-/funkciószűrőt ad; a háttérfrissítés nem írja
  felül a helyi szerkesztést. A vezérlési cél listája a `control_uuid` mezőből
  készül, nem a mérési UUID-ból. A parancstámogatás runtime ellenőrzése még hátravan.

## Formátum

```json
{
  "schema": 1,
  "consumers": [
    {
      "id": "dishwasher",
      "name": "Mosogatógép",
      "icon": "dishwasher",
      "source": {"uuid": "11223344-5566-7788-99aabbccddeeff00", "scale": 0.001},
      "average_kw": 1.4,
      "suggest_start": true
    }
  ],
  "cards": [
    {
      "id": "living-temperature",
      "name": "Nappali hőmérséklet",
      "icon": "temperature",
      "type": "value",
      "source": {"uuid": "11223344-5566-7788-99aabbccddeeff01", "scale": 1, "offset": 0},
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
      "source": {"uuid": "11223344-5566-7788-99aabbccddeeff02", "scale": 1}
    }
  ]
}
```

Az UUID-k szemléltetők, nem egy telepítés valós vezérlői. Az ikonkulcsok az `assets/loxone-home/catalog.json` készlethez tartoznak (a fenti
példakulcsok szemléltetők); a modell még csak a kulcs biztonságos
alakját vizsgálja, a tényleges fájlok meglétét a csomagkezelő fogja ellenőrizni.

## Korlátok és hibák

- Kezdeti keret oldalanként 12 elem; a végleges elrendezéshez igazítható konstans.
- Az `id` és `icon` 1–32 kisbetű, szám, kötőjel vagy aláhúzás; nincs fájlútvonal.
- Az azonosító egy adott listán belül egyedi. A név legfeljebb 80 UTF-8 bájt,
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

A hálózati struktúrából készülő katalógus az állapot `uuid` mellett most már
`control_uuid` mezőt is tartalmaz. Az értékek különbözhetnek; hiányzó vagy
érvénytelen vezérlőkulcsból nem szabad parancscélt készíteni.

## Elfogadott fogyasztói elrendezés

A 1280×800 Master-oldalon legfeljebb négy kártya van egy sorban, legfeljebb
három sorban. A kártyák mérete minden elemszámnál azonos. A hiányos utolsó sor
vízszintesen középre igazított, a két szélén azonos üres hellyel.
Hét elemnél négy kártya van az első sorban és három középre igazítva a másodikban.
Öt elemnél az utolsó egy kártya, hatnál az utolsó kettő kerül középre.

A közös geometria a `app_loxone_home_layout.h` `lx_home_card_rect()` függvénye.
A későbbi LVGL-oldal ezt hívja; a megjelenítőben ne legyen ettől eltérő másolt képlet.
Nulla vagy 12 feletti elemszám, hibás index és hiányzó kimenet elutasított.
Ez az elrendezési modell még nem a kész képernyő.

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
