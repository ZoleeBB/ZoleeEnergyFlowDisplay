# Loxone Program blokk

A kijelzőhöz szükséges JSON állapotcsomagot a Loxone Config **Program** blokkjában futó Pico C script állítja elő.

A kész minta: `examples/loxone-energy-payload.picoc`.

## Bekötés

| Program bemenet | JSON mező | Jelentés |
| --- | --- | --- |
| I1 | PowerGrid | Hálózati teljesítmény kW; pozitív = vételezés, negatív = visszatáplálás |
| I2 | PowerBat | Akkuteljesítmény kW; pozitív = kisütés, negatív = töltés |
| I3 | PowerPV | Napelem pillanatnyi termelése kW |
| I4 | SocBat | Akkumulátor töltöttsége % |
| I5 | PowerFull | Ház pillanatnyi fogyasztása kW |
| I6 | Temperature | Külső hőmérséklet °C |
| I7 | WindSpeed | Szélsebesség km/h |
| I8 | HasSunRadiation | 0 vagy 1 |
| I9 | HasPrecipitation | 0 vagy 1 |
| I10 | Status | 1..4 |
| I11 | NightMode | 0 vagy 1 |
| I12 | BatteryTemperature | Akkumulátor hőmérséklete °C |

A JSON a Program blokk **Txt1** kimenetére kerül 5 másodpercenként. A Txt1-et kell a már konfigurált MQTT publikálásra kötni.

## Status

- 1: Normál működés
- 2: Felkészülés áramszünetre
- 3: Szigetüzem
- 4: Hiba

## MQTT topic

Alapértelmezés:

`loxberry/mqttgateway/energymonitor/state`

A teljes JSON egy üzenetben kerül publikálásra.

## Hibakeresés

A Program blokk `Etxt` kimenetén jelennek meg az interpreter/szintaxis hibák. A script végtelen ciklusában szándékosan van `sleep(5000)`, hogy ne terhelje feleslegesen a Miniserv​ert.
