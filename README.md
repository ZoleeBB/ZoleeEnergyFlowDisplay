# Zolee Energy Flow Display

Otthoni energiaáramlás-kijelző a **Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1** hardverhez.

## Főbb jellemzők

- 1280x800, fix fekvő LVGL felület
- ESP-IDF 5.5.4
- MQTT adatforrás, QoS 1
- PV / ház / akkumulátor / hálózat energiaáramlás
- 5 perces napi grafikonok
- mai és tegnapi, azonos időpontig számolt összehasonlítás
- microSD konfiguráció és statisztika
- nappali / éjszakai mód
- helyi információs weboldal
- Wi-Fi OTA, két OTA partíció és automatikus rollback

## Első indítás

1. A `config.sample.json` alapján hozz létre egy `config.json` fájlt.
2. Másold a microSD gyökerébe: `/config.json`.
3. Állítsd be legalább a Wi-Fi SSID-t és az MQTT broker címét.
4. Indítsd el a készüléket.

A `config.json` szándékosan nincs verziókezelve.

## MQTT

Alapértelmezett topic:

`loxberry/mqttgateway/energymonitor/state`

Példa payload: `examples/mqtt-payload.sample.json`.

A kijelző minden kötelező mezőt ellenőriz. Hibás vagy hiányos csomag esetén az egész csomag elutasításra kerül.

## Loxone

A szükséges JSON összeállítására kész Pico C példa található itt:

`examples/loxone-energy-payload.picoc`

Részletes bekötés: `docs/loxone.md`.

## Fordítás

```bash
idf.py set-target esp32p4
idf.py build
```

A projekt célverziója: **ESP-IDF 5.5.4**.

## OTA / információs oldal

Hostname:

`zolee-energy-flow-display`

Információs oldal:

`http://zolee-energy-flow-display.local/`

Firmware frissítés:

`http://zolee-energy-flow-display.local/update`

## SD-kártya

```text
/config.json
/stats/YYYY-MM-DD.json
```

A rendszer 35 helyi naptári napot tart meg; a részletes mai/tegnapi adatok mellett heti és havi összesítés is elérhető.

## CI

A GitHub Actions minden main push és pull request esetén ESP-IDF 5.5.4 fordítást futtat.

## Dokumentáció

- [Teljes fejlesztési specifikáció](docs/development-specification.md)
- [Hardver](docs/hardware.md)
- [Fordítás és feltöltés](docs/build-flash.md)
- [microSD előkészítés](docs/sd-card.md)
- [Konfiguráció](docs/configuration.md)
- [MQTT protokoll](docs/mqtt.md)
- [Loxone Program blokk](docs/loxone.md)
- [Statisztikai számítások](docs/statistics.md)
- [Hibakezelés](docs/errors.md)
- [OTA és webfelület](docs/ota.md)
- [Működési áttekintés](docs/architecture.md)

- [Grafikai assetek](docs/assets.md)


## Release és közvetlen HTTP adatfogadás

A `main` ágra merge-ölt PR automatikusan új, eggyel növelt patch-verziójú release-t készít a sikeres tesztek és firmware-build után, OTA- és USB-csomagokkal. A puszta PR-jóváhagyás még nem indít kiadást.

A GitHubon kézzel publikált release és prerelease automatikusan teljes tesztet és ESP32-P4 firmware-buildet indít. A tag például `v1.2.0` vagy `v1.2.0-rc.1`; ez jelenik meg firmware-verzióként is. Az elkészült release `-ota.bin` fájlja közvetlenül az eszköz `/update` oldalára tölthető, a `-usb.zip` az első USB-s telepítő. Részletek: [release útmutató](docs/releases.md).

A konfiguráció gyökerében a `DataReceptMode` választható: `mqtt` (alapérték) vagy `http`. HTTP módban a `POST /api/energy` teljes JSON csomagot fogad, hitelesítés nélkül; MQTT broker nem szükséges. [Loxone 17.x bekötés és Pico C minta](docs/http-input.md), [teljes HTTP konfiguráció](examples/config.http.sample.json).

## UI kipróbálása eszköz nélkül, Visual Studio 2026 alatt

Nyisd meg a projekt gyökérmappáját, válaszd a **Windows UI demo (Debug, x64)** konfigurációt és a **zolee_simulator.exe** indítási célt, majd **F5**. A valódi kijelzőfelület 5 másodpercenként demó JSON-csomagot kap. [Telepítés, billentyűk, hibakeresés](docs/simulator.md).

## SD-sablonok

[Telepítés és séma](docs/templates.md) · [Saját sablon készítése, lépésről lépésre](docs/template-authoring.md)

## Új statisztikai téma és diagnosztika

Nyolc SD-s elrendezés, mindegyik nappali/sötét változattal; a rendszer alapnézete változatlan. Az `08-statistics` öt napi grafikonon mutatja a termelést, fogyasztást, előjeles hálózati és akkumulátorteljesítményt és SOC-ot. A pillanatnyi energiaáramlás csak számokkal jelenik meg. Heti/havi oszlopok, önellátási arányok és konfigurált tarifák szerinti pénzügyek is elérhetők.

Közös diagnosztika: felfelé húzás, vissza: lefelé; web: `/diagnostics`. A webes sablonkezelő a feltöltés előtt világos/sötét előnézetet és memória-becslést ad, majd telepítés nélkül ellenőriz az eszközön. [A tíz fejlesztés és a hat commit-szakasz](docs/enhancements-20261011.md).
