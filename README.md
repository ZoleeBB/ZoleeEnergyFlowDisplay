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

A rendszer a mai és a tegnapi napot tartja meg.

## CI

A GitHub Actions minden main push és pull request esetén ESP-IDF 5.5.4 fordítást futtat.

## Dokumentáció

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
