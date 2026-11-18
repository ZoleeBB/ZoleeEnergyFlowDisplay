# Konfiguráció

Az első üzembe helyezéshez a Wi-Fi megadható az érintőképernyőn. Sikeres időszinkron után nyisd meg a kijelzett `http://` címet, válassz üzemmódot, és mentsd a beállításokat. A dedikált 4B itt a Slave szerepet kínálja fel. [Első Wi-Fi-beállítás](first-run-wifi.md)

Előre beállított kártyához a [MQTT-mintát](../config.sample.json) vagy a [HTTP-mintát](../examples/config.http.sample.json), illetve a [közvetlen Loxone-mintát](../examples/config.loxone.sample.json) másold a FAT32 microSD gyökerébe `config.json` néven. A személyes konfiguráció nincs verziókezelve. Újraindításkor töltődik be.

| Rész | Beállítás |
| --- | --- |
| `DataReceptMode` | `mqtt` (alapérték), `http`, `loxone`, `inverter` vagy `slave` |
| `EnergyFlowDisabled` | 0/1, alapból 0; kizárólag Loxone módban: [Otthon/kaputelefon, logós készenlét és Slave-ek](energy-flow-disable.md) |
| `DataSeedEnabled` | 0/1, alapból 0: BLE master-adatmegosztás; Slave módban 0 szükséges |
| `DataSeedPin` | Hat számjegyű szöveg; üresen eszközön generált, tartós PIN. Master módban a diagnosztikán is látszik. Változtatása törli a BLE-párosításokat. |
| `ota.auto_update` | 0/1, alapból 0: [automatikus stabil GitHub-frissítés](ota.md#automatikus-éjféli-rendszerfrissítés) helyi napváltás után; internet és Wi-Fi kell hozzá |
| `project` | Cím, nyelv (`hu`/`en`), pénznem felirata |
| `wifi` | SSID, jelszó, újracsatlakozás; opcionális statikus IP, átjáró, maszk, DNS |
| `mqtt` | MQTT módban broker, port, topic, hitelesítés, timeout és újracsatlakozás |
| `http` | HTTP módban adatkimaradási timeout |
| `time` | NTP szerver, időzóna, nyári időszámítás és szinkronizálás |
| `display` | Nappali/éjszakai fényerő 1–100%; tájolás: `rotation_degrees` 270 vagy 90. A régi `touch_wake_seconds` 5–3600 (alapból 60) a Loxone-jelenlétprofil első betöltésének alapértéke; a nappali/éjszakai sötétítést és közös késleltetést már a Loxone oldalon állítsd be. |
| `statistics` | Mentési időköz és maximális integrálási adatrés |
| `energy` | Vételi és visszatáplálási egységár, CO₂- és faelnyelési tényező, `battery_capacity_kwh` (0 = ismeretlen), `battery_min_soc_pct` 0–90 (az akku hátralévő idejének alsó határa; 0 = alapértelmezés: normál üzemben 15%, áramszünetre készülve és szigetüzemben 5%) |
| `ota` | A kezelő webfelület közös Basic Auth felhasználóneve/jelszava |

HTTP módban MQTT broker nem szükséges. A kiválasztott mód adatkimaradási időkorlátja 5–3600 másodperc, alapértéke 30. A nappali/éjszakai módot a mérési csomag `NightMode` mezője vezérli.

A `display.rotation_degrees` csak `270` (csatlakozók jobb oldalon, alapérték) vagy `90` (csatlakozók bal oldalon) lehet. A GT911 érintéskezelése együtt fordul a képpel. A mező opcionális: régi konfigurációs fájloknál automatikusan 270° érvényes; más értéket az ellenőrzés elutasít. A kijelző az induláskor NVS-ből kiválasztott tájolással inicializálódik, és az SD-kártyát csak ezután olvassa. A betöltött konfiguráció (SD-vel `config.json`, SD nélkül belső JSON) a hiteles forrás: ha a tájolás eltér az NVS-ben tárolt értéktől, azt frissíti és egyszer újraindul. Futás közbeni LVGL-forgatás nincs, mert a MIPI DSI részleges framebufferrel hardveres összeomlást okozhat. Az első átállítás egy szándékos, vezérelt újraindítást eredményez. A korábbi 15 másodperces „próbaidőszak” megszűnt: az indulás közbeni újraindítás (frissítés, kézi reset) emiatt mindig visszaállt 270°-ra, pedig a 90°-os mód stabil. Az NVS most csak a kiválasztott tájolást tárolja.

A `Europe/Budapest` időzónát a firmware CET/CEST szabályként kezeli. `auto_dst=0` esetén fix `utc_offset_minutes` érvényes; más automatikus időzónához POSIX TZ szabály kell, nincs általános IANA-időzónaadatbázis. Alap NTP: `hu.pool.ntp.org`, ismételt szinkron 6 óránként, háttérbeli újrapróbálás 15 percenként.

Az OTA hitelesítésnél mindkét mező legyen kitöltve vagy mindkettő üres. Ez a sablonkezelőt és diagnosztikát is védi; a `POST /api/energy` nem kér hitelesítést. Az időjárás hiányát a JSON `null` értékek jelzik, nem konfigurációs kapcsoló.

## Beállítások a webes felületen

Az `/settings` oldalon a rendszerbeállítások módosíthatók, az `/api/settings` végpontok a `config.json` mentését és ellenőrzését végzik. A kijelző hálózati, adatfogadási módot érintő, időkezelési és más rendszerbeállításainak módosítása biztonságos újraindítással lép életbe. A statisztikai beállítások újraindítás nélkül érvényesülnek. A Master és az egyes Slave-ek jelenlét szerinti nappali/éjszakai sötétítése a külön [Loxone oldalon](loxone-direct.md#mester-jelenlét-szerinti-sötétítése) található.

Kártya nélkül az általános beállítások legfeljebb 3072 bájtos JSON-ként
a belső NVS-tárba menthetők. A működő, SD nélküli Slave beállítófelületén
a szerep rögzített. A `cardless_slave` az állapotjelentés része.

## Slave mód beállítása

SD-vel a minimális Slave konfiguráció `{"DataReceptMode":"slave"}`. Korábban
be nem állított, kártya nélküli eszközön az első webes beállításban válaszd
a **Slave** szerepet. Mentés és újraindítás után engedélyezd a Masteren az
adatmegosztást, nyisd meg a párosítási ablakot, majd a Master diagnosztikáján
látható PIN-t írd be a Slave érintőbillentyűzetén.

A Wi-Fi SSID/jelszó és az `ota.auto_update` kapcsoló a mastertől öröklődik;
az óra és a helyi UTC-eltolás is onnan érkezik. A Slave a Wi-Fi-n saját DHCP-címet
kap, a master statikus IP-jét és adminjelszavát nem másolja át. Automatikus OTA-hoz
a Slave saját internetkapcsolatot használ. A két SD-változat témái és statisztikái:
[SD-s és SD nélküli Slave](sd-card.md#slave-sd-kártyával-és-anélkül).

## Statisztikai mentés és jelszavak

A napi statisztika mentési időköze alapértelmezetten 15 perc, és legfeljebb 15 perc lehet. A korábban már SD-kártyán szereplő `save_interval_minutes: 60` beállítást a firmware automatikusan 15-re korlátozza. Az OTA firmware-frissítés és kézi újraindítás előtt ezen felül azonnali mentés történik.

A webes űrlap a Wi-Fi, MQTT és OTA jelszavakat nem olvassa vissza: üresen hagyva megtartja, külön törlési jelölővel kiüríti, új jelszó megadásával lecseréli azokat.

A háttérszinkron siker után a `time.resync_hours` teljes időközét várja ki;
a `background_retry_minutes` csak sikertelen szinkron vagy hiányzó Wi-Fi esetén érvényes.
Az órás időköz tickre váltása 64 bites számítást használ, így az 1000 Hz-es
FreeRTOS mellett sem rövidül a 6 óra körülbelül 125 másodpercre.

## Közvetlen Loxone mód

`DataReceptMode: "loxone"` esetén a külön [Loxone beállító](loxone-direct.md) kezeli a Miniserver adatait és a párosításokat. Ezek az SD `loxone.json` fájljába kerülnek; nem kell Loxone szekciót létrehozni az alapkonfigurációban. [Példakonfiguráció](../examples/config.loxone.sample.json).

A Master saját jelenlétprofilja ugyanitt, a `presence` objektumban tárolja a
`blank_at_day`, `blank_at_night` kapcsolókat és az 1–86400 másodperces
`timeout_seconds` késleltetést. Ez a jelenlét megszűnése és az utolsó érintés
után is érvényes; az érintés legalább 15 másodpercre ébreszt. A Slave-ek
önálló profiljai a Master belső tárában maradnak, és a védett BLE-kapcsolaton
jutnak el az adott kijelzőhöz.

A helyiség-/funkciószűrés, mértékegységek és a négy nappályamező részletei:
[Loxone adatkapcsolat](loxone-direct.md).

## Közvetlen inverter mód

Válaszd a **Közvetlen inverter** üzemmódot (`DataReceptMode: "inverter"`).
Újraindítás és időszinkron után az **Inverter beállítások** fület nyisd meg.
Itt találod a gyártóhoz és az adatkapcsolathoz tartozó mezőket, az akkumulátor
kapcsolóját, a helyszín koordinátáit, valamint a Master és a párosított
Slave kijelzők időprogramját. Az aktív profil mentése után megindul a mérések
és az időjárás lekérése. [Beállítások, támogatott modellek és adatfrissítés](direct-inverter.md)

## Mentés és visszaállítás

SD-s kijelzőn a **Mentés** oldal a támogatott konfigurációs fájlokat tölti le
egyetlen JSON-ként. A belső tartalékból új SD-kártyára is visszaállíthatók.
Az általános konfiguráció az `inverter` mód kiválasztását tartalmazza.
A külön NVS-ben tárolt inverterprofil, annak hitelesítési adatai, a koordináták
és a heti programok kimaradnak az SD-alapú mentésből. Másik Master üzembe
helyezésekor ezeket az **Inverter beállítások** fülön add meg.
[Mentés és visszaállítás](backup.md)

A Loxone Adatpárosításban a külső hőmérséklet után opcionális, szorzóval/eltolással
beállítható [relatív páratartalom](relative-humidity.md) is megadható.
