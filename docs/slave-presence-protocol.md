# Slave jelenlétprofil – modell és átviteli szerződés

Állapot: a modell, a Master NVS-háttérmentése, a célzott védett BLE-olvasás,
a közös Loxone WebSocket és a két panel adaptere be van kötve. A webes
eszközlista/szerkesztő még hátravan; az újonnan párosított profilban a
sötétítés alapból ki van kapcsolva. Fizikai paneles/radiós próba még szükséges.

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

UUID: `8b7e1006-6c8a-4f57-9b2e-5d1a7c0e9f30`.
A jelenlegi karakterisztika csak célzott READ-et ad; a Slave másodpercenként
olvassa, a három kötelező energia/idő notify setupját változatlanul hagyva.
A GATT-flagek READ_ENC/READ_AUTHEN és min_key_size=16 értékűek; minden access
külön ellenőrzi a meglévő encrypted/authenticated/bonded/16-byte szabályt.
A profilt az adott hitelesített kapcsolat feloldott identitásából választjuk,
nem kliens által küldött slotból vagy eszköznévből.

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
A jelenlét csak sikeres hitelesítés és katalógus után engedélyezett, már az
enablebinstatusupdate ACK előtt érkező kezdő delta is megmarad.
A 45 másodperces frissességi határt a közös Loxone heartbeat tartja; disconnect
invalidál. Jelenlét-UUID cseréjekor a worker a meglévő socketet újraépíti a teljes
kezdő állapothoz; második Loxone-kapcsolat nincs.
A felfutó él azonnal növeli a számlálót, ezért két BLE-poll közé eső 1→0
állapot sem veszik el. Több Slave ugyanazt a jelenlétforrást is használhatja.
A 0 érvényes inaktív, pozitív véges szám aktív; negatív/NaN/infinite/invalid
nem érvényes aktivitás. WS-kapcsolatvesztés minden forrást invalidál.
A BLE `live` jel a teljes meglévő Loxone-kapcsolat frissességét ellenőrizze;
egy változatlan állapot-UUID önmagában nem évül el.

Minden új BLE-mintához új sorszám szükséges. Csak pozitív, fél számtérnél kisebb
unsigned különbség léphet előre; ismétlés, visszalépés és pontosan fél számtér
elutasítandó. Profilváltáskor a számláló új bázis lehet. A kapcsolati adapter
resetje kötelező, mielőtt egy új kapcsolatról adatot ad át. A sorba állított
üzenetek és aszinkron callbackek kapcsolati generációt is ellenőrizzenek;
az újrahasznált connection handle önmagában nem védi ki a korábbi kapcsolat
késői válaszát. Régi üzenet az új kapcsolat bázisát sem állíthatja be.

## Fogadás és kijelzőállapot

Új kapcsolat, profilrevízió és legalább 15 másodperc csend utáni első csomag
csak számlálóbázis: régi impulzust nem játszik vissza. A friss, tényleges aktív
szint ugyanakkor ébren tarthat. Lejárt vagy hibás mintából nem lesz ébresztés.

A receiver rövid külön zár alatt kezelendő; a host a tényleges BLE-fogadáskor,
e zár megszerzése után vett monoton idővel hívja a dekódert. A BLE host nem
várhat LVGL-zárra. A kijelzőadapter az inputkapuval azonos LVGL-zár alatt,
e rövid receiver-zárat is megszerezve fusson; fordított zársorrend nincs.
Az adapter e zár után új monoton időt vesz, így egy korábban megkezdett UI-tick
nem dobhatja el a közben érkezett friss BLE-impulzust.
Teljes vevőállapot/snapshot adható át, de egy puszta „utolsó wire-csomag” sor
önmagában nem őrzi meg a régi impulzus saját érkezési idejét. Ha több csomag
érkezik egy UI-frissítés előtt, a függő impulzus megmarad a következő friss,
érvényes csomag mellett, és egyszer fogy el. Az impulzus saját fogadási ideje külön megmarad: egy későbbi inaktív heartbeat
nem hosszabbítja meg az esemény 15 másodperces érvényességét. Invalid csomag vagy
új bázis eldobja a régi függő impulzust. A fogadási idő monoton idő, nem falióra.

A kijelzőadapter kizárólag a sötétítés/timeout mezőket veszi át. A Slave saját
nappali/éjszakai fényereje, az utolsó ismert napszaka, a boot/PIN láthatósága és
az első teljes ébresztő érintést elnyelő kapu a meglévő közös állapotgépben marad.
Tartós aktív jel tart; érvényes lefutó él egyszer új timeoutot ad.
Profilváltás/invalid/disconnect nem ad új timeoutot és nem szimulál lefutó élt.
Disconnectkor a profil a memóriában megmarad; csak a tényleges legacy átállás kapcsolja ki.

## Master NVS és háttérmentés

A `ble_presence` névtér `peers` blobja 410 bájt. A 4 bájtos fejléc:
ASCII `ZS`, verzió=1, slotok=3. Három 134 bájtos slot követi, majd 4 bájt
little-endian CRC32 (IEEE, a megelőző 406 bájtra).

| Sloton belüli offset | Hossz | Tartalom |
| --- | --- | --- |
| 0 | 1 | Használt slot: 0/1 |
| 1 | 1 | Normalizált identitástípus |
| 2 | 6 | Identitáscím NimBLE bájtsorrendben |
| 8 | 81 | Név, NUL és nulla kitöltés |
| 89 | 36 | Loxone állapot-UUID, NUL és nulla kitöltés |
| 125 | 4 | Timeout másodpercben, little-endian |
| 129 | 1 | Éjszakai sötétítés: 0/1 |
| 130 | 4 | Nem nulla profilrevízió, little-endian |

Üres slot minden bájtja nulla. A dekóder CRC, mezővalidálás és identitás-egyediség
után újrakódolással ellenőrzi a kanonikus kitöltést. A C-struktúra paddingje nem
kerül tárolásra. Impulzus, aktív szint, sorszám és fényerő nem része a blobnak.

Az egyhelyes értesítési sor mögötti 4096 bájtos háttértask csak konfigurációs
revízióeltéréskor nyit NVS-t. Az állapotzárat I/O előtt elengedi. Hibánál a
mentett revízió nem lép, és legfeljebb 5 másodpercenként próbál újra.
A publikus snapshot mutatja a konfiguráció/mentett revízió és hiba eltérését;
a webes adapternek ezt is meg kell jelenítenie. Az elavult szerkesztési revízió
elutasítandó; a Master bootonként véletlen kezdőrevíziót kap.
A Slave fogadott profilja disconnect alatt memóriában marad; külön Slave NVS-cache
nem készült. A saját fényerőinek meglévő belső konfigurációját ez nem módosítja.

Az opcionális olvasás request-generációt használ. Négy másodperc után a függő
kérés lejárt; késői válasza nem ébreszthet. Amíg az ATT-kérés még függőben van,
nem indul mellé újabb kérés. Read-hiba nem jelent legacy Mastert. Hibás payload/
replay nem törölheti a számlálóbázist és nem fiatalíthatja meg az érvényességet.

## Ellenőrzés és korlátok

`python3 tests/test_slave_presence.py`: a tényleges C modell és
`app_slave_display.c` közös UBSan-tesztje. A teljes regresszió is futtatja.
A modell nem foglal heapet; fordítási guard: legfeljebb 512 bájt a hárompeer-es
tábla, legfeljebb 64 bájt a receiver, pontosan 20 bájt a csomag.
A `tests/test_slave_presence_service.py` a valódi NVS-workert és GATT access
handlert futtatja RTOS/NVS/transport mockokkal, hibainjektálással. A tényleges
central callbackek tesztje late/replay/timeout/legacy eseteket is futtat.
A Loxone RSA/AES/HMAC protokollteszt a valódi WS-frissítőtől ellenőrzi az impulzust.
A CI mindkét ESP-IDF firmware-t és a Linux/Windows szimulátorokat is fordítja.
Ezek nem helyettesítik a fizikai panel/radio/DMA/PSRAM/FPS próbát.
