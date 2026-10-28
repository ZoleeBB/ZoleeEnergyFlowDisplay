# Konfiguráció

A [MQTT-mintát](../config.sample.json) vagy a [HTTP-mintát](../examples/config.http.sample.json), illetve a [közvetlen Loxone-mintát](../examples/config.loxone.sample.json) másold a FAT32 microSD gyökerébe `config.json` néven. A személyes konfiguráció nincs verziókezelve. Újraindításkor töltődik be.

| Rész | Beállítás |
| --- | --- |
| `DataReceptMode` | `mqtt` (alapérték), `http`, `loxone` vagy `slave` |
| `EnergyFlowDisabled` | 0/1, alapból 0; kizárólag Loxone módban: [Otthon/kaputelefon, logós készenlét és Slave-ek](energy-flow-disable.md) |
| `DataSeedEnabled` | 0/1, alapból 0: BLE master-adatmegosztás; Slave módban 0 szükséges |
| `DataSeedPin` | Hat számjegyű szöveg; üresen eszközön generált, tartós PIN. Master módban a diagnosztikán is látszik. Változtatása törli a BLE-párosításokat. |
| `ota.auto_update` | 0/1, alapból 0: [automatikus stabil GitHub-frissítés](ota.md#automatikus-éjféli-rendszerfrissítés) helyi napváltás után; internet és Wi-Fi kell hozzá |
| `project` | Cím, nyelv (`hu`/`en`), pénznem felirata |
| `wifi` | SSID, jelszó, újracsatlakozás; opcionális statikus IP, átjáró, maszk, DNS |
| `mqtt` | MQTT módban broker, port, topic, hitelesítés, timeout és újracsatlakozás |
| `http` | HTTP módban adatkimaradási timeout |
| `time` | NTP szerver, időzóna, nyári időszámítás és szinkronizálás |
| `display` | Nappali/éjszakai fényerő 1–100%; tájolás: `rotation_degrees` 270 vagy 90; `touch_wake_seconds` 5–3600 (alapból 60): Loxone-jelenlétvezérlésnél ennyi ideig marad ébren érintés után |
| `statistics` | Mentési időköz és maximális integrálási adatrés |
| `energy` | Vételi és visszatáplálási egységár, CO₂- és faelnyelési tényező, `battery_capacity_kwh` (0 = ismeretlen), `battery_min_soc_pct` 0–90 (az akku hátralévő idejének alsó határa; 0 = alapértelmezés: normál üzemben 15%, áramszünetre készülve és szigetüzemben 5%) |
| `ota` | A kezelő webfelület közös Basic Auth felhasználóneve/jelszava |

HTTP módban MQTT broker nem szükséges. A kiválasztott mód adatkimaradási időkorlátja 5–3600 másodperc, alapértéke 30. A nappali/éjszakai módot a mérési csomag `NightMode` mezője vezérli.

A `display.rotation_degrees` csak `270` (csatlakozók jobb oldalon, alapérték) vagy `90` (csatlakozók bal oldalon) lehet. A GT911 érintéskezelése együtt fordul a képpel. A mező opcionális: régi konfigurációs fájloknál automatikusan 270° érvényes; más értéket az ellenőrzés elutasít. A kijelző az induláskor NVS-ből kiválasztott tájolással inicializálódik, és az SD-kártyát csak ezután olvassa. A betöltött konfiguráció (SD-vel `config.json`, SD nélkül belső JSON) a hiteles forrás: ha a tájolás eltér az NVS-ben tárolt értéktől, azt frissíti és egyszer újraindul. Futás közbeni LVGL-forgatás nincs, mert a MIPI DSI részleges framebufferrel hardveres összeomlást okozhat. Az első átállítás egy szándékos, vezérelt újraindítást eredményez. A korábbi 15 másodperces „próbaidőszak” megszűnt: az indulás közbeni újraindítás (frissítés, kézi reset) emiatt mindig visszaállt 270°-ra, pedig a 90°-os mód stabil. Az NVS most csak a kiválasztott tájolást tárolja.

A `Europe/Budapest` időzónát a firmware CET/CEST szabályként kezeli. `auto_dst=0` esetén fix `utc_offset_minutes` érvényes; más automatikus időzónához POSIX TZ szabály kell, nincs általános IANA-időzónaadatbázis. Alap NTP: `hu.pool.ntp.org`, ismételt szinkron 6 óránként, háttérbeli újrapróbálás 15 percenként.

Az OTA hitelesítésnél mindkét mező legyen kitöltve vagy mindkettő üres. Ez a sablonkezelőt és diagnosztikát is védi; a `POST /api/energy` nem kér hitelesítést. Az időjárás hiányát a JSON `null` értékek jelzik, nem konfigurációs kapcsoló.

## Beállítások a webes felületen

Az `/settings` oldalon minden beállítás módosítható, az `/api/settings` végpontok a `config.json` mentését és ellenőrzését végzik. A kijelző hálózati, MQTT/HTTP/Loxone, időkezelési és más rendszerbeállításainak módosítása biztonságos újraindítással lép életbe. A statisztikai beállítások újraindítás nélkül érvényesülnek.

SD nélküli Slave esetén ugyanez a felület belső NVS-be ment, legfeljebb 3072 bájtos
JSON-ként. Ilyenkor az adatfogadási mód rögzítetten `slave`, a master kapcsoló tiltott;
az API is visszautasítja az eltérő szerepet. A `cardless_slave` visszaadott állapotjelző,
nem felhasználói konfigurációs kapcsoló.

## Slave mód beállítása

SD-vel a minimális konfiguráció `{"DataReceptMode":"slave"}`. MQTT-, HTTP-, Loxone-
és NTP-beállítás nem kell. Új, korábban fő kijelzőként nem azonosított eszköz SD
nélkül automatikusan Slave lesz. A masteren engedélyezd az adat seedet, nyisd meg
a kétperces párosítási ablakot, majd a master diagnosztikáján látható hatjegyű PIN-t
írd be a Slave érintős billentyűzetén.

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

A helyiség-/funkciószűrés, mértékegységek és a négy nappályamező részletei:
[Loxone adatkapcsolat](loxone-direct.md).

## Mentés és visszaállítás

A beállítások a webfelület **Mentés** oldalán egy fájlba menthetők és
visszatölthetők; a kijelző a saját flash-tárában is őriz egy példányt, amelyből
új SD-kártyánál automatikusan visszaállít. Részletek: [Mentés és visszaállítás](backup.md).
