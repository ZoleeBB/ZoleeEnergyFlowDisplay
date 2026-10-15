# Loxone Program blokk

A kijelzőhöz szükséges JSON állapotcsomagot a Loxone Config **Program** blokkjában futó Pico C script állítja elő.

A két teljes, különálló minta:

- **MQTT:** [`examples/loxone-energy-payload.picoc`](../examples/loxone-energy-payload.picoc)
- **HTTP POST:** [`examples/loxone-energy-http-post.picoc`](../examples/loxone-energy-http-post.picoc)

Csak az egyiket kell a választott `DataReceptMode` szerint a Program blokkba másolni.

## Bekötés

**Összesen 14 felhasznált bemenet van:** 13 analóg (`I1`–`I13`) és egy szöveges (`T1`). A Program blokkon **nincs I14**; a 14. érték a felső **T1** csatlakozón érkezik. A `getinputtext(0)` visszatérési értéke foglalt karakterlánc, amit a minta minden ciklusban felszabadít `free()` hívással.


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
| I13 | WeatherPresent | Időjárási jelenlétmaszk, 0–15 (nem külön JSON mező) |
| **14. – T1 (első szöveges bemenet)** | **SunPosition** | Opcionális `azimut#napmagasság#napkeltePerc#napnyugtaPerc`; üresen `null` |

A JSON a Program blokk **Txt1** kimenetére kerül 5 másodpercenként. MQTT módban a Txt1-et a publikálásra, HTTP módban a POST virtuális kimeneti parancshoz kösd. **Mindkét Pico C mintában ugyanaz az I1–I13 + T1 bemeneti kiosztás.**

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

## Közvetlen HTTP POST bővítés

A `DataReceptMode=http` módhoz közvetlen POST-minta tartozik: [loxone-energy-http-post.picoc](../examples/loxone-energy-http-post.picoc). A [HTTP bekötési útmutató](http-input.md) a Miniserver Gen 2 / 17.x beállításait tartalmazza. A firmware mindkét fogadási módot támogatja; a konfiguráció választja ki az aktív módot.

## Időjárás jelenlétjelzés (I13)

Az **I1–I12 sorrendje változatlan**. A minták az eddig szabad **I13** bemenetet
használják `WeatherPresent` jelenlétmaszkként; ez nem lesz új JSON-mező.
A dokumentált Pico C API-ban nincs bekötöttség-lekérdezés: a `getinput()`
értéket ad vissza, a `getinputevent()` változást jelez. Emiatt a bekötetlen
bemenetet nem lehet megbízhatóan megkülönböztetni egy valódi nulla értéktől.
A jelenlétet ezért a Loxone Configban kell megadni, vagy adatérvényességi jelekből
előállítani. **A script nem ismeri fel önállóan a vezeték csatlakoztatását.**

| Bekötött és érvényes adat | A maszkhoz hozzáadandó érték |
| --- | ---: |
| I6: Temperature | 1 |
| I7: WindSpeed | 2 |
| I8: HasSunRadiation | 4 |
| I9: HasPrecipitation | 8 |

Minden időjárásbemenet használatakor I13 = **15**. Ha például csak a hőmérséklet
és a szél van bekötve, I13 = **3**. Időjárás nélkül I13 = **0** (bekötetlen
I13 esetén is ez az alaphelyzet). A nem használt bemenet bitjét 0-ra kell
állítani: ekkor annak JSON-értéke `null`, miközben a kulcs megmarad. Egy
jel leválasztásakor a maszkot is módosítani kell. Ha érzékelőadat kimarad,
a maszk megfelelő bitjét annak adatérvényességi jelével kell kikapcsolni.
Negatív, 15 feletti vagy tört maszk biztonságosan 0-ra esik vissza.
A valódi 0 °C, 0 km/h és 0 állapotjel bekötött/jelenlévő bit mellett megmarad.

Hivatalos API: [Loxone Custom Script Programming](https://www.loxone.com/dede/wp-content/uploads/sites/2/2016/08/CustomScriptProgramming.pdf).
A megjelenítési szabályok: [MQTT adatprotokoll](mqtt.md#opcionális-időjárási-értékek).

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

## Opcionális nappálya (T1 szöveges bemenet)

Az első szöveges **T1** bemenetre a Loxone-ban összefűzött
`azimut#napmagasság#napkeltePerc#napnyugtaPerc` szöveg kerül.
Példa: `327.445#-46.562#420#1077`.
A fokok 0–360/−90–+90 tartományúak; a napkelte/napnyugta a helyi éjféltől
eltelt percek száma. A minta Pico C program `getinputtext(0)` hívással olvassa,
és `SunPosition` néven továbbítja. Bekötetlen vagy üres szöveg esetén
`"SunPosition":null` kerül a csomagba. Az I1–I13 analóg bemenetek változatlanok.
A firmware csak teljes és tartományhelyes adatot fogad el.
