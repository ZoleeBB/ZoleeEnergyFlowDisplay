# Slave jelenlétprofil – modell és átviteli szerződés

Állapot: a modell, a Master NVS-háttérmentése, a célzott védett BLE-olvasás,
a közös Loxone WebSocket, a két panel adaptere és a Master/Loxone webes
eszközlista/szerkesztő be van kötve. Az újonnan párosított profilban a
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
| 3 | 1 | Bit 0: **sötétítési kérés most**; bit 1: érvényes jelenlét; bit 2: aktív szint; bit 3-4: kijelző tájolása negyedfordulatban (0–3); bit 5: frissítésellenőrzés kérése |
| 4 | 4 | Nem nulla profilrevízió |
| 8 | 4 | Timeout másodpercben, 1–86400 |
| 12 | 4 | Jelenlét felfutóéleinek modulo-2^32 számlálója |
| 16 | 4 | Kapcsolaton belüli modulo-2^32 csomagsorszám |

A 6–7. bit tiltott, aktív bit csak érvényes bittel együtt lehet.
A 0 fokos tájolás bitjei nullák, így a korábbi, a 3–7. bitet még tiltó Slave is
elfogadja az alapértelmezett csomagot; elforgatott kijelzőhöz a Slave firmware-ét
is frissíteni kell.
Egy revízión belül timeout és tájolás nem változhat (ezek profiladatok); a 0.
bit, az érvényes/aktív bit és a frissítéskérés bitje viszont a revízión belül is
változik, mert ezek pillanatnyi állapotok.

**A sötétítésről a Master dönt** (v3.3.4-től), mert csak ő látja a Loxone
jelenlétállapotát. A 0. bit nem a profil „éjszakai sötétítés" beállítása, hanem
pillanatnyi kérés, és három feltétel együtt kell hozzá:

1. a profilban be van kapcsolva az éjszakai sötétítés;
2. ha van kiválasztott jelenlétforrás: a Loxone-kapcsolat friss (`live`) **és**
   a forrás érvényes értéket adott;
3. a jelenlét érvényesen megszűnt (0 értéket kaptunk), és a 1→0 él óta letelt a
   profil időkorlátja.

A Loxone **csak változáskor** küld értéket, ezért a jelenlét órákig ugyanaz az
1 vagy 0 lehet: a Master a megkapott állapotot tartja, és a `live` frissességét a
15 másodperces keepalive adja, nem az állapot-UUID változása. A jelenlétvesztés
ideje a WS-kapcsolat újraépülését is túléli, különben az újra megkapott,
változatlan 0 újraindítaná a visszaszámlálást, és a kijelző kigyulladna egy
időkorlátra az éjszaka közepén.

Jelenlétforrás nélküli profilnál (csak érintéses ébresztés) a bit az éjszakai
beállítást követi. Néma, még fel sem épült vagy érvénytelen forrásnál a bit 0:
a kijelző inkább maradjon világos, mint hogy folyamatos jelenlét alatt aludjon
el. Ez a 3.3.3-ig meglévő hiba gyökere: a Slave próbálta helyben kitalálni, mit
jelent a „nincs érvényes minta”, és a legrosszabb irányba tippelt.

A frissítéskérés **szintjel**: a Master a webes „frissítések keresése" gomb után
60 másodpercig küldi, a Slave pedig a felfutó élre indít egyetlen ellenőrzést a
saját kiadására (`ZoleeEnergyFlowDisplay-slave-4b-<tag>-ota.bin`). Ismételt
olvasás, visszajátszás és a kapcsolat újraépülése nem indít újabb letöltést; a
Wi-Fi és a futó művelet ellenőrzése a közös OTA-modulé. A napváltáskor induló
automatikus ellenőrzést a Slave eddig is magától futtatta. A kliens minden hibát
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

A kijelzőadapter kizárólag a sötétítés/timeout mezőket veszi át; a tájolást a
Slave célprogramja olvassa ki (`app_slave_presence_service_rotation`), mert futó
MIPI DSI mellett nincs átforgatás: a 4B a saját `slave_4b`/`rotation` NVS-kulcsába
menti, és rövid késleltetés után újraindul, hogy a kép és az érintés is a kért
tájolásban induljon. Kaputelefon-hívás vagy PIN-bevitel alatt vár az újraindítással. A Slave saját
nappali/éjszakai fényereje, az utolsó ismert napszaka, a boot/PIN láthatósága és
az első teljes ébresztő érintést elnyelő kapu a meglévő közös állapotgépben marad.
Tartós aktív jel tart; érvényes lefutó él egyszer új timeoutot ad.
Profilváltás/invalid/disconnect nem ad új timeoutot és nem szimulál lefutó élt.
Érvénytelen minta (lejárt lease, kimaradt olvasás, kapcsolatvesztés) nem
jelenlétvesztés: az utolsó ismert állapot marad érvényben. A jelenléttartás
15 másodperc (egy-két kimaradt olvasás áthidalása) – nem ez védi a kijelzőt a
sötétedéstől, hanem a Master 0. bitje, ami csak tényleges jelenlétvesztés után
engedi el. Kapcsolatvesztéskor a Master utolsó döntése marad érvényben, a helyi
érintés viszont mindig ébreszt: legalább 15 másodpercre, akkor is, ha a profil
időkorlátja ennél rövidebb.

Az érintés a nyers inputkapuból érkezik, ezért a vaksötét kijelzőn is hat (az
első teljes érintést a kapu elnyeli, csak ébreszt). Nyitott diagnosztika alatt a
Slave nem sötétít, és legalább 30% fényerőt ad, hogy a hibakeresés a legalacsonyabb
éjszakai fényerő mellett is olvasható legyen.

A 4B háttérvilágítása GPIO26-on LEDC PWM (invertált kimenet, 10 bit, 5 kHz), a
GT911 érintés pedig I2C-polling INT/RST nélkül: a 0% fényerő tényleg teljesen
lekapcsolja a világítást, de az érintést nem érinti. A 0% kizárólag a sötétítési
ágból jöhet, a nappali/éjszakai fényerő érvényességi köre 1–100%.
Disconnectkor a profil a memóriában megmarad; csak a tényleges legacy átállás kapcsolja ki.

## Master NVS és háttérmentés

A `ble_presence` névtér `peers` blobja a v2 formátumban **416 bájt**.
A 4 bájtos fejléc ASCII `ZS`, verzió=2, slotok=3. Három **136 bájtos**
slot követi, majd 4 bájt little-endian CRC32 (IEEE). A korábbi, 410 bájtos
v1 változat is betölthető: ott három 134 bájtos slot van, és a fényerő
két opcionális mezője alapértelmezés szerint öröklésre áll. Az első módosításkor
a háttérmentés az új v2 formátumot írja.

| Sloton belüli offset | Hossz | Tartalom |
| --- | --- | --- |
| 0 | 1 | Használt slot: 0/1 |
| 1 | 1 | Normalizált identitástípus |
| 2 | 6 | Identitáscím NimBLE bájtsorrendben |
| 8 | 81 | Név, NUL és nulla kitöltés |
| 89 | 36 | Loxone állapot-UUID, NUL és nulla kitöltés |
| 125 | 4 | Timeout másodpercben, little-endian |
| 129 | 1 | Bit 0: éjszakai sötétítés; bit 1-2: tájolás negyedfordulatban |
| 130 | 4 | Nem nulla profilrevízió, little-endian |
| 134 | 1 | Opcionális nappali fényerő: 0 = Master, 1–100 = százalék (csak v2) |
| 135 | 1 | Opcionális éjszakai fényerő: 0 = Master, 1–100 = százalék (csak v2) |

A tájolás a sötétítés bájtjának szabad bitjeire került, ezért a blob mérete és a
korábbi verzióval mentett profilok változatlanul betöltődnek (0 fokkal).
Üres slot minden bájtja nulla. A dekóder CRC, mezővalidálás és identitás-egyediség
után újrakódolással ellenőrzi a kanonikus kitöltést. A C-struktúra paddingje nem
kerül tárolásra. Impulzus, aktív szint és sorszám nem része a blobnak; a beállított
fényerő-felülírások viszont tartósan mentettek.

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

## Master/Loxone HTTP és webes szerkesztő

A `/loxone` oldalon a Slave-kijelzők külön szakaszban szerepelnek. A helyiség és
funkció szűrő a meglévő Loxone-katalógust használja. A kiválasztott UUID akkor is
megmarad, ha a szűrés vagy egy átmenetileg hiányzó katalógus elrejtené. Nincs
kézzel beírható eszközidentitás vagy automatikus profil-/bond-kiszorítás.

A `GET /api/loxone/slaves` és `POST /api/loxone/slaves` csak Loxone módban
regisztrált, a meglévő webes hitelesítéssel. A GET `Cache-Control: no-store` mellett
`schema:1`, `enabled`, `ready`, `revision`, `saved_revision`, `save_error` és
`profiles` mezőket ad. Minden profil az `id`, `name`, `presence_uuid`,
`timeout_seconds`, `blank_at_night`, `rotation_degrees` (0/90/180/270) mezőket
tartalmazza. Futási jelenlétminta,
számláló, fényerő és hitelesítő adat nem kerül ebbe a válaszba. Kikapcsolt seed
mellett az eszközlista üres, a POST nem érhető el (503).

A POST kötelező fejléce `X-Zolee-Loxone: 1`, törzse legfeljebb 2048 bájt:

```json
{"schema":1,"revision":123,"id":"ble-0-060504030201","profile":{"name":"Nappali","presence_uuid":"11223344-5566-7788-99aabbccddeeff00","timeout_seconds":30,"blank_at_night":false,"rotation_degrees":0}}
```

A pontos mezőkészlet, szigorú típusok, UTF-8 név, kanonikus UUID, egész timeout és
0/90/180/270 fokos tájolás kötelező. Duplikált/idegen mező, beágyazott NUL, túlméret és JSON utáni szöveg
elutasítandó (400). Az ID kizárólag a már ismert, hitelesített peer táblájában
kereshető; a kliens nem adhat új identitáscímet. A nem nulla teljes `uint32_t`
revíziót lebegőpontos JSON-számból tartományellenőrzés után vesszük át. Az elavult
revíziót az adapter és az állapotzár alatti szolgáltatás is elutasítja (409).
A HTTP-task nem végez NimBLE- vagy NVS-műveletet.

A sikeres POST `202 {"accepted":true}`: ez még nem tartós mentés. A web csak akkor
jelez mentett beállítást, ha visszaolvasva a kívánt profil található, nincs
`save_error`, és `saved_revision == revision`. A tartós mentési hiba külön látható,
kézzel és a meglévő háttértaskkal is újrapróbálható. Elveszett POST-válasznál csak
GET ellenőrzés indul, automatikus újraküldés nincs.

A kétmásodperces frissítés megőrzi a helyi draftokat és a beviteli DOM-elemeket.
Az ugyanarra a profilra érkező külső változás nem írja felül a szerkesztést, és
mentés előtt külön ütközést jelez. Másik peer változásánál friss revízióval
menthető a saját változat. A POST közbeni újabb változást a backend 409-e védi.
A szerkesztés elvetése a legutóbb visszaolvasott profilt tölti vissza.

## Ellenőrzés és korlátok

`python3 tests/test_slave_presence.py`: a tényleges C modell és
`app_slave_display.c` közös UBSan-tesztje. A teljes regresszió is futtatja.
A modell nem foglal heapet; fordítási guard: legfeljebb 512 bájt a hárompeer-es
tábla, legfeljebb 64 bájt a receiver, pontosan 20 bájt a csomag.
A `tests/test_slave_presence_service.py` a valódi NVS-workert és GATT access
handlert futtatja RTOS/NVS/transport mockokkal, hibainjektálással. A tényleges
central callbackek tesztje late/replay/timeout/legacy eseteket is futtat.
A Loxone RSA/AES/HMAC protokollteszt a valódi WS-frissítőtől ellenőrzi az impulzust.
`tests/test_loxone_home_store.py`: tényleges Slave HTTP-kezelők és valódi profilmodell,
hitelesítés/mód/korlátok/revízió/hibás JSON és cJSON-allokációs hibák.
`node tests/test_slave_presence_web.js`: a kiadott JS három eszközzel, draft- és
DOM-megőrzéssel, ütközéssel, mentési hibával és elveszett válasszal.
A CI mindkét ESP-IDF firmware-t és a Linux/Windows szimulátorokat is fordítja.
Ezek nem helyettesítik a fizikai panel/radio/DMA/PSRAM/FPS próbát.

## Slave-enkénti fényerő és átadás (2026-11)

A Master Loxone oldalának Slave-kártyáin a nappali és éjszakai fényerő
külön-külön állítható 1–100% között. Üres mező esetén a Master megfelelő
beállítása öröklődik. Az API `day_brightness` és `night_brightness` mezői
ilyenkor `null` értékűek; NVS-ben a 0 jelöli a felülírás hiányát.

A hitelesített 1004 BLE-profil olvasásakor a Master kizárólag a feloldott
peer identitásához tartozó felülírásokat teszi a már létező 102 bájtos
profil 100/101-es fényerőbájtjaiba, egyenként. Ez nem változtatja a BLE
profil méretét vagy az OTA/Wi-Fi mezőket. A kijelzők futás közben
alkalmazzák a változást újraindítás nélkül. A korábbi BLE-formátum
korlátja megmarad: 63–64 karakteres Wi-Fi-jelszó mellett nincs hely
a profilban a két fényerőbájtnak, így az ilyen speciális konfiguráció
fényerő-szinkronját külön meg kell vizsgálni.
