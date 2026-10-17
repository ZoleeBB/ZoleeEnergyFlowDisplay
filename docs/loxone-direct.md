# Közvetlen Loxone adatforrás (v2.0.0)

Az elsődleges cél a helyi Gen2/v17 Miniserver. Régebbi generáció működése külön próbát igényel.

A `config.json` egyetlen új értéke: `"DataReceptMode": "loxone"`.
A Wi-Fi/NTP és a többi általános beállítás ugyanott marad. Módváltáskor a webes
beállításmentő újraindítja az eszközt. Hiányzó paramétereknél fekete,
Loxone-logós képernyő mutatja a paraméterezéshez szükséges helyi webcímet.

A kezdőlap és a Beállítások oldal Loxone módban elérhetővé teszi a
`/loxone` oldalt. Előbb Miniserver IP/gépnév, port, felhasználó és jelszó,
majd „Kapcsolat tesztelése és adatlista lekérése”. Ez aszinkron művelet:
a webfelület a háttérfeladat állapotát mutatja, nem blokkolja a HTTP kiszolgálót.
A lista csak a vizualizációban elérhető, a felhasználónak engedélyezett elemeket
kínálja fel. Javasolt külön kategória és felhasználó, virtuális állapotokkal.

A kötelező mezők: PowerGrid, PowerBat, PowerPV, PowerFull, SocBat, Status,
NightMode és BatteryTemperature. Az időjárási négy mező kihagyható (null),
a nappálya opcionális, négy külön numerikus párosítással: `SunAzimuth` (fok,
0 ≤ érték < 360), `SunAltitude` (fok, −90…90), `Sunrise` és `Sunset`
(egész perc éjféltől, 0…1439, napkelte < napnyugta). Például 07:00 = 420.
Mind a négy párosítást meg kell adni, vagy mind elhagyható; a nappálya az összes
kezdőérték beérkezése után jelenik meg. Szorzó és eltolás itt is használható.
A régi Loxone `SunPosition` szöveges párosítás helyett ezeket újra párosítani kell;
a többi mentett párosítás megmarad. Az MQTT/HTTP bemenet `SunPosition` szöveges
formátuma változatlan.
Virtuális analóg állapotnál `value`, digitálisnál `active`.
A lista két legördülővel, helyiség és funkció (Loxone-kategória) szerint szűrhető,
a két feltétel egyszerre érvényes. A lista neve elöl áll: `Név (Helyiség / Funkció) · állapot`.
A korábbi párosítás szűrés közben is megmarad; a mentett kulcs az állapot UUID-ja.
Számok átalakítása: `érték * scale + offset`. W → kW: 0.001; előjelváltás: −1;
digitális invertálás: scale=−1, offset=1. A Status a kijelző 1–4 kódját követi.
A hálózati és akku-előjeleket a meglévő [adatprotokoll](mqtt.md) szerint kell beállítani.

## Külön SD-konfiguráció és eszközkulcs

A Miniserver adatai és párosításai kizárólag `loxone.json`-ba kerülnek.
A jelszó AES-256-GCM titkosítást kap, minden mentéskor új 96 bites nonce-szal és
128 bites hitelesítő címkével. A host, port és felhasználó is hitelesített adat.
A véletlen 256 bites kulcs a készülék NVS-ében él, nem az SD-kártyán.
Ez az SD önálló kiolvasása ellen véd; nem helyettesít flash encryption/secure boot
védelmet a készülék fizikai feltörésével szemben. NVS-törlés vagy másik kijelző
esetén a párosítások betöltődnek, a jelszót újra meg kell adni.
A webes API sem jelszót, sem titkosítványt, sem tokent nem ad vissza.
Mentés: tmp + flush/fsync + korábbi fájl biztonsági másolata + átnevezés;
megszakadt átnevezés után bootkor a biztonsági másolat helyreállítható.
Sikertelen SD-mentéskor a korábbi konfiguráció marad aktív.

## Kapcsolat és hibák

Helyi WS: titkosított RSA/AES Loxone-hitelesítés és JWT, de az állapotesemények
nem kapnak teljes adatfolyam-titkosítást. WSS (Gen2) esetén megbízható CA PEM-et
kell megadni, és a hostnak egyeznie kell a tanúsítvánnyal; nincs automatikus
ellenőrzés-kikapcsolás. A webes beállító ugyanazt az opcionális Basic Auth védelmet
használja, mint a meglévő eszközweb; helyi HTTP, nem HTTPS.

Az adatlista először WebSocketen érkezik. Ha a fájlkérés 404-es választ ad,
a kliens ugyanarról a Miniserverről HTTP(S)-en kéri le a `data/LoxAPP3.json`
fájlt. Ehhez friss HTTP hitelesítési kulcsból HMAC-et készít a meglévő JWT-vel;
a jelszó és a nyers token nem kerül a kérésbe. A HTTP(S) a kiválasztott WS/WSS
módot és a CA-ellenőrzést követi. Átirányítást nem követ, 401/403 esetén nem
próbál más jogosultságot. A fájl méretkorlátja ezen az úton is 512 KiB.

A kliens 15 másodpercenként Loxone keepalive parancsot küld; 45 másodpercnyi
bejövő üzenethiányt vagy azonnali socket/Wi-Fi hibát kapcsolatvesztésnek vesz.
A hibaképernyőről automatikusan tér vissza a témákra, amikor újra beérkezett az
összes kötelező állapot. Újrapróbálkozás: 2/4/8/16/30 s, legfeljebb 30 s.
A változatlan érték érvényes: az aktuális állapottárból 5 másodpercenként készül
validált minta. Ez a Miniserver kapcsolatát igazolja, nem az inverter upstream
adatainak frissességét. A Loxone oldali mérési érvényesség külön logikai feladat.
Kapcsolatvesztéskor és újrabeállításkor az integrálási lánc megszakad, kiesésen át
nem számolunk energiát. Az új verzió OTA-érvényesítéséhez Loxone módban az elérhető
webfelület elegendő, nem kell 3 percen belül elvégezni az első párosítást.

## Erőforráskorlátok és ellenőrzés

Egy WebSocket, 8 elemű üzenetsor, legfeljebb 512 KiB struktúra/üzenet és 512
listázott állapot. A nagyobb adatlista hibával elutasított, nem részlegesen elfogadott.
A nem használt állapotok nem kerülnek tartós állapottárba. A kezdeti teljes JSON
feldolgozásakor átmeneti többletmemória szükséges. A JWT RAM-ban él. Megújítása a Miniserver `validUntil` időpontjából számított hátralévő idő felénél, legfeljebb óránként történik. Ismeretlen lejárat vagy még hiányzó pontos rendszeridő esetén az órás ütemezés érvényes. A sikeres megújítás nem bontja a WebSocketet és nem törli az állapottárat. Ha elutasítják, ugyanazon a kapcsolaton új tokent kérünk; csak kapcsolat- vagy hitelesítési hiba indít újracsatlakozást. Újraindításkor új hitelesítés történik.

A platformfüggetlen C-tesztek a valódi UUID/formátumkezelést, szorzókat,
hiányzó kezdőértékeket, null időjárást, numerikus/szöveges eseménytáblákat és hibás
csomagokat ellenőrzik. Tényleges Miniserver és fizikai kijelző nélküli teszt nem
igazolja a gyártói firmware-kompatibilitást vagy a panel FPS/memóriatartalékát.
Kiadás előtt: valódi Gen2/v17 kapcsolódás, lejárat/megújítás, jogosultságváltozás,
router/Miniserver újraindítás, SD-hiba, kártyaköltöztetés, 100 témaváltás és
legalább 24 órás járatás szükséges a belső RAM/DMA minimumának megfigyelésével.

Forrás: [Communicating with the Miniserver v17](https://www.loxone.com/enen/kb/api/),
[Structure File](https://www.loxone.com/enen/kb/api/),
[LxCommunicator.NET](https://github.com/Loxone/LxCommunicator.NET).

### Elvégzett ellenőrzések (2026-10-17)

- Teljes ESP-IDF 5.5.4 / ESP32-P4 fordítás: sikeres. Alkalmazás: 0x288090 bájt,
  a 8 MiB partícióban 68% szabad hely. Ez flash-kapacitás, nem RAM/FPS mérés.
- Teljes korábbi regressziós tesztsor és az új C-adatfeldolgozási teszt: sikeres.
- MbedTLS 3.6.5-tel valódi RSA/AES/HMAC kézfogás, tokenmegújítás,
  WS/driver-fragmentáció és túlcsordulás tesztje: sikeres.
- Valódi AES-GCM és fájlrendszeres eszközkulcs-/rongálás-/mentéshiba-tesztek: sikeresek.
- Valódi LVGL render mind a 18 témaváltozattal, külön Loxone beállító,
  kapcsolódási és kapcsolathiba-képernyő, automatikus visszatérés: sikeres.
- Valódi Miniserver és fizikai panel: még nincs ellenőrizve.
