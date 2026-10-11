# Konfiguráció

A [MQTT-mintát](../config.sample.json) vagy a [HTTP-mintát](../examples/config.http.sample.json) másold a FAT32 microSD gyökerébe `config.json` néven. A személyes konfiguráció nincs verziókezelve. Újraindításkor töltődik be.

| Rész | Beállítás |
| --- | --- |
| `DataReceptMode` | `mqtt` (alapérték) vagy `http` |
| `project` | Cím, nyelv (`hu`/`en`), pénznem felirata |
| `wifi` | SSID, jelszó, újracsatlakozás; opcionális statikus IP, átjáró, maszk, DNS |
| `mqtt` | MQTT módban broker, port, topic, hitelesítés, timeout és újracsatlakozás |
| `http` | HTTP módban adatkimaradási timeout |
| `time` | NTP szerver, időzóna, nyári időszámítás és szinkronizálás |
| `display` | Nappali/éjszakai fényerő, százalékban 1–100 |
| `statistics` | Mentési időköz és maximális integrálási adatrés |
| `energy` | Vételi és visszatáplálási egységár, CO₂- és faelnyelési tényező |
| `ota` | A kezelő webfelület közös Basic Auth felhasználóneve/jelszava |

HTTP módban MQTT broker nem szükséges. A kiválasztott mód adatkimaradási időkorlátja 5–3600 másodperc, alapértéke 30. A nappali/éjszakai módot a mérési csomag `NightMode` mezője vezérli.

A `Europe/Budapest` időzónát a firmware CET/CEST szabályként kezeli. `auto_dst=0` esetén fix `utc_offset_minutes` érvényes; más automatikus időzónához POSIX TZ szabály kell, nincs általános IANA-időzónaadatbázis. Alap NTP: `hu.pool.ntp.org`, ismételt szinkron 6 óránként, háttérbeli újrapróbálás 15 percenként.

Az OTA hitelesítésnél mindkét mező legyen kitöltve vagy mindkettő üres. Ez a sablonkezelőt és diagnosztikát is védi; a `POST /api/energy` nem kér hitelesítést. Az időjárás hiányát a JSON `null` értékek jelzik, nem konfigurációs kapcsoló.
