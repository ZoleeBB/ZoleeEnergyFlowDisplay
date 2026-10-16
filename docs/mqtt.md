# MQTT adatprotokoll

A kijelző egyetlen MQTT topicról kapja az egész pillanatnyi állapotot.

Alapértelmezett topic:

`loxberry/mqttgateway/energymonitor/state`

- transport: sima TCP
- alapértelmezett port: 1883
- QoS: fixen 1
- ajánlott publikálás: retained
- frissítés: 5 másodpercenként

A firmware nem használ payload timestampet és nem próbálja a retained üzenet korát meghatározni.

## Kötelező mezők

| Mező | Tartomány | Jelentés |
| --- | ---: | --- |
| PowerGrid | -15 .. +15 kW | pozitív = hálózati vételezés, negatív = visszatáplálás |
| PowerBat | -15 .. +15 kW | pozitív = akkumulátor kisütés, negatív = töltés |
| PowerPV | -0.05 .. 15 kW | negatív mérési zaj 0-ra normalizálva |
| SocBat | 0 .. 100 % | töltöttség |
| PowerFull | -0.05 .. 15 kW | ház fogyasztása; negatív mérési zaj 0-ra normalizálva |
| Temperature | -35 .. +50 °C | külső hőmérséklet |
| WindSpeed | 0 .. 150 km/h | szélsebesség |
| HasSunRadiation | 0 vagy 1 | napsugárzás állapot |
| HasPrecipitation | 0 vagy 1 | csapadék állapot |
| Status | 1 .. 4 | rendszerállapot |
| NightMode | 0 vagy 1 | nappali / éjszakai mód |
| BatteryTemperature | -20 .. +80 °C | akkumulátor hőmérséklet |

A mezőnevek kis-/nagybetű érzéketlenek. Ha ugyanaz a mező eltérő írásmóddal kétszer szerepel, a teljes csomag hibás.

Ismeretlen extra JSON mezők megengedettek és figyelmen kívül maradnak.

A négy időjárási mező null kivételétől eltekintve, ha bármely kötelező mező hiányzik, rossz típusú vagy tartományon kívüli, a teljes payload elutasításra kerül. A kijelző és a statisztika csak teljesen érvényes csomagtól frissül.

## Status

- 1: Normál működés
- 2: Felkészülés áramszünetre
- 3: Szigetüzem
- 4: Hiba

Szigetüzemben a fő energiaábra a hálózatot leválasztottnak mutatja, de a statisztika továbbra is a tényleges PowerGrid mérési értéket használja.

## Adatkimaradás

A kijelző az utolsó érvényes értékeket megtartja a konfigurált `data_timeout_seconds` idejéig. Alapérték: 30 másodperc.

A timeout után teljes képernyős MQTT-adathiány hiba jelenik meg. Érvényes csomag érkezésekor a dashboard automatikusan visszatér.


## Opcionális időjárási értékek

A 12 JSON-kulcs továbbra is kötelező. A `Temperature`, `WindSpeed`,
`HasSunRadiation` és `HasPrecipitation` értéke lehet JSON `null` (idézőjelek
nélkül). A numerikus 0 érvényes adat, nem hiányjelzés. A többi nyolc mező
változatlanul nem lehet null; különösen a `NightMode` vezérli a teljes témát,
és a `BatteryTemperature` akkumulátoradat.

A hőmérséklet és a szél külön-külön jelenik meg, ha van adat. Az időjárásikon
csak érvényes hőmérséklet-, napsugárzás- és csapadékadat mellett látszik,
hogy hiányzó állapotból ne keletkezzen hamis napos/esős/havas jelzés.
Ez a rendszertémára és minden SD-témára, nappali és éjszakai módban is érvényes.
Egy új null érték azonnal elrejti a korábbi kijelzést; az adat visszatérése
újra megjeleníti. Az energiaadatok feldolgozása és a timeout működése folytatódik.
Minta: [mqtt-payload.no-weather.sample.json](../examples/mqtt-payload.no-weather.sample.json).

Az ikonválasztás sorrendje minden témában azonos: csapadék esetén −4 °C alatt
hó, egyébként eső; csapadék nélkül éjszakai módban éjszaka ikon. Nappal
`HasSunRadiation = 1` **vagy `PowerPV > 5` kW** esetén napos ikon látszik.
Egyébként 1 kW felett változóan felhős, legfeljebb 1 kW-nál felhős az állapot.
Pontosan 5 kW önmagában még nem vált napos ikonra. A szükséges időjárási
mezők érvényességi feltétele változatlan.

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

## Opcionális nappályaadatok

A `SunPosition` mező opcionális: hiányozhat vagy értéke `null` lehet.
Ha nem null, akkor az első Loxone-szövegbemenetből érkező, négy részből álló
`azimut#napmagasság#napkeltePerc#napnyugtaPerc` szöveget tartalmazza.
Példa: `"SunPosition":"327.445#-46.562#420#1077"`.
Az azimut [0,360), a napmagasság [-90,90] fokban, a percek [0,1439]
tartományban értendők. A napkelte a napnyugta előtt kell legyen.
Érvénytelen, nem null érték esetén a csomag elutasításra kerül.
