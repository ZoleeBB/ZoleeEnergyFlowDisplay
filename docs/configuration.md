# Konfiguráció

A [MQTT-mintát](../config.sample.json) vagy a [HTTP-mintát](../examples/config.http.sample.json), illetve a [közvetlen Loxone-mintát](../examples/config.loxone.sample.json) másold a FAT32 microSD gyökerébe `config.json` néven. A személyes konfiguráció nincs verziókezelve. Újraindításkor töltődik be.

| Rész | Beállítás |
| --- | --- |
| `DataReceptMode` | `mqtt` (alapérték), `http`, `loxone` vagy `slave` |
| `DataSeedPin` | Hat számjegyű szöveg; üresen eszközön generált, tartós PIN. Master módban a diagnosztikán is látszik. Változtatása törli a BLE-párosításokat. |
| `ota.auto_update` | 0/1, alapból 0: [automatikus stabil GitHub-frissítés](ota.md#automatikus-éjféli-rendszerfrissítés) helyi napváltás után; internet és Wi-Fi kell hozzá |
| `project` | Cím, nyelv (`hu`/`en`), pénznem felirata |
| `wifi` | SSID, jelszó, újracsatlakozás; opcionális statikus IP, átjáró, maszk, DNS |
| `mqtt` | MQTT módban broker, port, topic, hitelesítés, timeout és újracsatlakozás |
| `http` | HTTP módban adatkimaradási timeout |
| `time` | NTP szerver, időzóna, nyári időszámítás és szinkronizálás |
| `display` | Nappali/éjszakai fényerő 1–100%; tájolás: `rotation_degrees` 270 vagy 90 |
| `statistics` | Mentési időköz és maximális integrálási adatrés |
| `energy` | Vételi és visszatáplálási egységár, CO₂- és faelnyelési tényező, `battery_capacity_kwh` (0 = ismeretlen) |
| `ota` | A kezelő webfelület közös Basic Auth felhasználóneve/jelszava |

HTTP módban MQTT broker nem szükséges. A kiválasztott mód adatkimaradási időkorlátja 5–3600 másodperc, alapértéke 30. A nappali/éjszakai módot a mérési csomag `NightMode` mezője vezérli.

A `display.rotation_degrees` csak `270` (csatlakozók jobb oldalon, alapérték) vagy `90` (csatlakozók bal oldalon) lehet. A GT911 érintéskezelése együtt fordul a képpel. A mező opcionális: régi konfigurációs fájloknál automatikusan 270° érvényes; más értéket az ellenőrzés elutasít. A kijelző az induláskor NVS-ből kiválasztott tájolással inicializálódik, és az SD-kártyát csak ezután olvassa. A `config.json` a hiteles forrás: ha a tájolás eltér az NVS-ben tárolt értéktől, azt frissíti és egyszer újraindul. Futás közbeni LVGL-forgatás nincs, mert a MIPI DSI részleges framebufferrel hardveres összeomlást okozhat. A 90°-os indulás 15 másodperc után válik igazolttá; ha addig újraindulás történik, a következő boot automatikusan 270°-ra tér vissza, és letiltja a 90°-os újrapróbálást. Feloldás: válassz 270°-ot, mentsd, majd ismét 90°-ot. Az első átállítás két egymást követő, szándékos újraindítást eredményezhet.

A `Europe/Budapest` időzónát a firmware CET/CEST szabályként kezeli. `auto_dst=0` esetén fix `utc_offset_minutes` érvényes; más automatikus időzónához POSIX TZ szabály kell, nincs általános IANA-időzónaadatbázis. Alap NTP: `hu.pool.ntp.org`, ismételt szinkron 6 óránként, háttérbeli újrapróbálás 15 percenként.

Az OTA hitelesítésnél mindkét mező legyen kitöltve vagy mindkettő üres. Ez a sablonkezelőt és diagnosztikát is védi; a `POST /api/energy` nem kér hitelesítést. Az időjárás hiányát a JSON `null` értékek jelzik, nem konfigurációs kapcsoló.

## Beállítások a webes felületen

Az `/settings` oldalon minden beállítás módosítható, az `/api/settings` végpontok a `config.json` mentését és ellenőrzését végzik. A kijelző hálózati, MQTT/HTTP/Loxone, időkezelési és más rendszerbeállításainak módosítása biztonságos újraindítással lép életbe. A statisztikai beállítások újraindítás nélkül érvényesülnek.

A napi statisztika mentési időköze alapértelmezetten 15 perc, és legfeljebb 15 perc lehet. A korábban már SD-kártyán szereplő `save_interval_minutes: 60` beállítást a firmware automatikusan 15-re korlátozza. Az OTA firmware-frissítés és kézi újraindítás előtt ezen felül azonnali mentés történik.

A webes űrlap a Wi-Fi, MQTT és OTA jelszavakat nem olvassa vissza: üresen hagyva megtartja, külön törlési jelölővel kiüríti, új jelszó megadásával lecseréli azokat.

A háttérszinkron siker után a `time.resync_hours` teljes időközét várja ki;
a `background_retry_minutes` csak sikertelen szinkron vagy hiányzó Wi-Fi esetén érvényes.
Az órás időköz tickre váltása 64 bites számítást használ, így az 1000 Hz-es
FreeRTOS mellett sem rövidül a 6 óra körülbelül 125 másodpercre.

## Közvetlen Loxone mód

`DataReceptMode: "loxone"` esetén a külön [Loxone beállító](loxone-direct.md) kezeli a Miniserver adatait és a párosításokat. Ezek az SD `loxone.json` fájljába kerülnek; nem kell Loxone szekciót létrehozni az alapkonfigurációban. [Példakonfiguráció](../examples/config.loxone.sample.json).

## Közvetlen Loxone mód

Az alapfájlban csak `DataReceptMode: "loxone"` szükséges. A Miniserver címe, portja, felhasználója, titkosított jelszava és a párosítások külön `loxone.json` fájlba kerülnek; a `/loxone` felületen szerkeszthetők. Helyiség-/funkciószűrés, mértékegységek és a négy nappályamező részletei: [Loxone adatkapcsolat](loxone-direct.md).
