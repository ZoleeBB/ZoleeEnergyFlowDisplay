# OTA és helyi webfelület

Hostname:

`zolee-energy-flow-display`

Információs oldal:

`http://zolee-energy-flow-display.local/`

Firmware feltöltés:

`http://zolee-energy-flow-display.local/update`

## Hitelesítés

A `ota.username` és `ota.password` mezők közös HTTP Basic hitelesítést adnak az információs oldalhoz, az OTA feltöltéshez és az eszköz újraindításához, a sablonkezelőhöz és a diagnosztikához.

Ha mindkettő üres, nincs hitelesítés.

## Két OTA partíció

A firmware két alkalmazáspartíciót használ. Az új image először próbaállapotban indul.

Az új firmware legfeljebb 3 percen belül akkor minősül sikeresnek, ha:

- SD-kártya elérhető
- konfiguráció sikeresen betöltődött
- Wi-Fi kapcsolat létrejött
- NTP szinkron sikeres
- MQTT módban a brokerkapcsolat létrejött; HTTP módban a fogadó szerver elindult

Érvényes mérési csomag érkezése nem feltétele az OTA firmware elfogadásának.

Ha a feltételek nem teljesülnek, a bootloader visszaáll az előző működő firmware-re.

## Információs oldal

Megjeleníti többek között:

- firmware verzió
- uptime
- IP cím
- Wi-Fi RSSI
- MQTT kapcsolat állapota
- utolsó érvényes mérési csomag ideje
- SD állapot
- reset oka
- legutóbbi fontos események / hibák

Az oldalon külön megerősítéssel újraindítható az eszköz.

A közös diagnosztika a `/diagnostics` és `/api/diagnostics` útvonalon érhető el. A `POST /api/energy` adatfogadás hitelesítés nélkül működik; [HTTP útmutató](http-input.md).

## SD-beállítások böngészőből

A `/settings` oldalon a `config.json` minden dokumentált paramétere módosítható. A `GET /api/settings` az aktuális beállításokat adja vissza, a jelszavakat `null` értékkel helyettesíti (nem küldi ki). A `POST /api/settings` JSON tartalmú, `X-Zolee-Settings: 1` fejlécet igényel; a `null` jelszó megtartja a korábbit, a nem üres szöveg módosítja, az üres szöveg törli.

Mentéskor a firmware ideiglenes fájlba ír, a tartalmat ugyanazzal a konfigurációs validátorral ellenőrzi, majd a régi fájlról biztonsági másolatot készít. Egy megszakadt fájlcsere esetén a korábbi `config.json.webbak` következő indításkor visszaállítható. A statisztikai mentési és integrációs paraméterek újraindítás nélkül érvényesülnek, minden más konfigurációváltozás automatikus újraindítást indít.

**Fontos:** A Basic Auth titkosítás nélkül, HTTP-n működik, ezért a beállításkezelést csak megbízható helyi hálózaton használd. Az üres adminisztrátori felhasználónév/jelszó azt jelenti, hogy a felület nincs védve.

## Biztonságos firmware-frissítés

A feltöltőoldal százalékos előrehaladást és egyértelmű siker/hiba üzeneteket mutat. Az OTA firmware csak az utolsó statisztikai SD-mentés sikeres befejezése után aktiválódik. Kézi újraindítás előtt is azonnali mentés fut; ha ez sikertelen, az újraindítás elmarad és hibaüzenet jelenik meg.

Az eseménynapló a legutóbbi 20 RAM-bejegyzést jeleníti meg. A napló az eszköz újraindításakor törlődik, nem tartós hibahistória.

A gyári Napi mérleg, Nyugodt otthon, Energiakör és Energiafőkönyv témák szín-/háttérjavításait az OTA után a kijelző a régebbi SD-sablonokon is alkalmazza. A teljesen új sablonfájlok telepítéséhez továbbra is a webes sablonkezelő vagy az SD-re másolás használható.
