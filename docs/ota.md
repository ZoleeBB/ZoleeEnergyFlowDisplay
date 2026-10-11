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
