# Slave jelenlétprofil – modell és átviteli szerződés

Állapot: a tiszta modell és annak natív tesztje elkészült; az éles BLE/NVS/Loxone/
web/UI integráció még hátravan. A fordítási forráslistán szereplő új modul
önmagában nem kapcsolja be az éjszakai sötétítést.

## Identitás és jogosultság

A Master profilja a NimBLE kapcsolati leíró **feloldott `peer_id_addr`**
identitásához tartozzon. A host adapter csak az encrypted + authenticated +
bonded + 16 bájtos kulcs ellenőrzése után regisztrálhat. A tiszta modell nem tudja
helyettesíteni ezt az ellenőrzést, és nem fogad önbevallott Slave-azonosítót.
A cím típusa a modellben public=0 vagy random-static=1; az adapter normalizálja
a NimBLE megfelelő identity típusát. RPA, non-resolvable cím és hirdetési cím
nem használható profiltulajdonosként.

Három slot van, automatikus kiszorítás nélkül. A stabil logikai kulcs
`ble-<type>-<12 kisbetűs hex>`, a cím a szokásos megjelenítési bájtsorrendben.
Az ismert peer újracsatlakozása nem nevezi át a már szerkesztett profilt.
A név legfeljebb 80 UTF-8 bájt, a timeout 1–86400 másodperc.
Az üres jelenlét-UUID megengedi a csak érintésre ébresztő profilt.
A Loxone UUID pontosan 35 karakter, kisbetűs `8-4-4-16` alak.

A web kizárólag hitelesített, korlátos snapshotot és szerkesztési parancssort
kapjon; HTTP-taskból nem hívható NimBLE. A tartós konfiguráció külön NVS-
háttérmentést kapjon, csak valódi változáskor. A `changed` eredmény a szemantikai
mezőket hasonlítja, nem a C-struktúra paddingjét. Jelenlétminta, számláló,
érintés, lease és sorszám nem tartós adat, nem írhat flash-t.

## Opcionális karakterisztika

Tervezett UUID: `8b7e1006-6c8a-4f57-9b2e-5d1a7c0e9f30`.
Csak célzott read/notify; írás nincs. Mind a GATT-flagekben, mind az access/
notify adapterben maradjon a meglévő szintű titkosítás, MITM/bond és 16 bájtos
kulcsellenőrzés. A profilt az adott hitelesített kapcsolat identitásából kell
kiválasztani, nem a kliens által küldött slotból vagy eszköznévből.

A régi `1004` Wi-Fi/OTA profil **102 bájtos, v1 formátuma változatlan**.
A jelenlét nem kérhet Wi-Fi-újraindítást. A régi Master opcionális `1006`
nélkül továbbra is használható, igazolt legacy állapotban sötétítés nélkül.
GATT-hiba, hitelesítésvesztés vagy ideiglenes timeout önmagában nem legacy jelzés.

## Wire v1

A 20 bájt a 23 bájtos ATT MTU értesítési payloadjába is belefér.
A több bájtos számok unsigned little-endian értékek.

| Offset | Hossz | Tartalom |
| --- | --- | --- |
| 0 | 2 | ASCII `ZP` |
| 2 | 1 | Verzió: 1 |
| 3 | 1 | Bit 0: éjszakai sötétítés; bit 1: érvényes jelenlét; bit 2: aktív szint |
| 4 | 4 | Nem nulla profilrevízió |
| 8 | 4 | Timeout másodpercben, 1–86400 |
| 12 | 4 | Jelenlét felfutóéleinek modulo-2^32 számlálója |
| 16 | 4 | Kapcsolaton belüli modulo-2^32 csomagsorszám |

Minden más flag tiltott, aktív bit csak érvényes bittel együtt lehet.
Egy revízión belül timeout/sötétítés nem változhat. A kliens minden hibát
atomikusan utasít el; hibás csomag nem frissíti a lease-t.

A Master **minden beérkező Loxone WS-deltánál** hívja a termelőmodellt.
A felfutó él azonnal növeli a számlálót, ezért két BLE-poll közé eső 1→0
állapot sem veszik el. Több Slave ugyanazt a jelenlétforrást is használhatja.
A 0 érvényes inaktív, pozitív véges szám aktív; negatív/NaN/infinite/invalid
nem érvényes aktivitás. WS-kapcsolatvesztés minden forrást invalidál.
A BLE `live` jel a teljes meglévő Loxone-kapcsolat frissességét ellenőrizze;
egy változatlan állapot-UUID önmagában nem évül el.

Minden új BLE-mintához új sorszám szükséges. Csak pozitív, fél számtérnél kisebb
unsigned különbség léphet előre; ismétlés, visszalépés és pontosan fél számtér
elutasítandó. Profilváltáskor a számláló új bázis lehet. A kapcsolati adapter
resetje kötelező, mielőtt egy új kapcsolatról adatot ad át.

## Fogadás és kijelzőállapot

Új kapcsolat, profilrevízió és legalább 15 másodperc csend utáni első csomag
csak számlálóbázis: régi impulzust nem játszik vissza. A friss, tényleges aktív
szint ugyanakkor ébren tarthat. Lejárt vagy hibás mintából nem lesz ébresztés.

A fogadó és az inputkapu közös állapotának műveletei ugyanazon LVGL-zár alatt
fussanak; a BLE host korlátos üzenetet/snapshotot adjon át. Ha több csomag
érkezik egy UI-frissítés előtt, a függő impulzus megmarad a következő friss,
érvényes csomag mellett, és egyszer fogy el. Invalid csomag vagy új bázis
eldobja a régi függő impulzust. A fogadási idő monoton idő, nem falióra.

A kijelzőadapter kizárólag a sötétítés/timeout mezőket veszi át. A Slave saját
nappali/éjszakai fényereje, az utolsó ismert napszaka, a boot/PIN láthatósága és
az első teljes ébresztő érintést elnyelő kapu a meglévő közös állapotgépben marad.
Tartós aktív jel tart; érvényes lefutó él egyszer új timeoutot ad.
Profilváltás/invalid/disconnect nem ad új timeoutot és nem szimulál lefutó élt.
Disconnectkor a profil megmarad; csak a tényleges legacy átállás kapcsolja ki.

## Ellenőrzés és korlátok

`python3 tests/test_slave_presence.py`: a tényleges C modell és
`app_slave_display.c` közös UBSan-tesztje. A teljes regresszió is futtatja.
A modell nem foglal heapet; fordítási guard: legfeljebb 512 bájt a hárompeer-es
tábla, legfeljebb 64 bájt a receiver, pontosan 20 bájt a csomag.
Ez nem GATT/NVS integrációs vagy fizikai paneles megfelelőségi állítás.
