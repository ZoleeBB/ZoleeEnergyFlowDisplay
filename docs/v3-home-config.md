# v3 Loxone otthonkonfiguráció – fejlesztői szerződés

## Megvalósítási állapot

Az `app_loxone_home_model.c` platformfüggetlen konfigurációs modell elkészült.
**A webes szerkesztő, a tartós mentés, az élő adatfogadás, a parancsküldés és az új
képernyők még nincsenek hozzákapcsolva.** Ez nem telepítési útmutató és nem kész
felhasználói funkció. A teljes feladat és a folytatás: [fejlesztési terv](v3-development-plan.md).

A modell külön objektum; nem növeli meg a több helyen másolt, jelszót is tartalmazó
`lx_config_t` hitelesítési konfigurációt. A modellben nincs token vagy jelszó.
A készüléken csak Master és közvetlen Loxone mód számára lesz felhasználva.

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

Az UUID-k szemléltetők, nem egy telepítés valós vezérlői. Az ikonkulcsok a később
elkészülő SD-s katalógushoz tartoznak; a modell még csak a kulcs biztonságos
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

## Ellenőrzés

```sh
python3 tests/test_loxone_home.py
```

A valódi C-modulokkal futó teszt oda-vissza JSON-kezelést, elemszámhatárt,
hibás párosítást, nem véges számokat, opcionális átlagteljesítményt, külön
parancs-/állapot-UUID-t és minden JSON-szerializálási foglalási pont hibáját vizsgálja.
A teszt UBSan és 8192 bájtos stack-frame fordítási korlát mellett fut.
Ez nem paneles RAM-/FPS-mérés és nem Miniserver-parancsküldési próba.
