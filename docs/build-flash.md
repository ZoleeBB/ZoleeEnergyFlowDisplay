# Fordítás és firmware feltöltés

## Követelmény

A projekt rögzített fejlesztői verziója:

**ESP-IDF 5.5.4**

Más ESP-IDF verzió használata előtt külön kompatibilitási ellenőrzés szükséges.

## Első fordítás

ESP-IDF terminálból a repository gyökerében:

```bash
idf.py set-target esp32p4
idf.py build
```

A Component Manager az első konfiguráláskor letölti a projekt függőségeit, többek között a Waveshare BSP-t és az LVGL-t.

## Feltöltés

Windows alatt a megfelelő COM porttal:

```bash
idf.py -p COM5 flash monitor
```

A `COM5` helyére a tényleges port kerüljön.

Csak flash:

```bash
idf.py -p COM5 flash
```

Csak soros monitor:

```bash
idf.py -p COM5 monitor
```

Kilépés a monitorból:

`Ctrl+]`

## Teljes törlés

Ha tiszta telepítés szükséges:

```bash
idf.py -p COM5 erase-flash
idf.py -p COM5 flash monitor
```

## GitHub Actions firmware

A `main` ág minden kódmódosításakor ESP-IDF 5.5.4 build fut.

Sikeres build után artifactként elérhetők:

- alkalmazás bináris
- bootloader
- partíciós tábla
- flasher_args.json

A normál fejlesztői telepítéshez továbbra is az `idf.py flash` használata javasolt, mert automatikusan a megfelelő címekre írja az összes szükséges binárist.

## OTA frissítés

Az első USB-s telepítés után a későbbi firmware az eszköz helyi weboldalán is feltölthető:

`http://zolee-energy-flow-display.local/update`

OTA-hoz a build könyvtárból a fő alkalmazás bináris kell:

`build/zolee_energy_flow_display.bin`


## Előre fordított firmware

A GitHub release mellékletei és az automatikus build leírása: [releases.md](releases.md). Az USB-csomag magyar telepítési útmutatója: [usb-install.md](usb-install.md).

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).
