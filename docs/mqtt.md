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

Ha bármely kötelező mező hiányzik, rossz típusú vagy tartományon kívüli, a teljes payload elutasításra kerül. A kijelző és a statisztika csak teljesen érvényes csomagtól frissül.

## Status

- 1: Normál működés
- 2: Felkészülés áramszünetre
- 3: Szigetüzem
- 4: Hiba

Szigetüzemben a fő energiaábra a hálózatot leválasztottnak mutatja, de a statisztika továbbra is a tényleges PowerGrid mérési értéket használja.

## Adatkimaradás

A kijelző az utolsó érvényes értékeket megtartja a konfigurált `data_timeout_seconds` idejéig. Alapérték: 30 másodperc.

A timeout után teljes képernyős MQTT-adathiány hiba jelenik meg. Érvényes csomag érkezésekor a dashboard automatikusan visszatér.
