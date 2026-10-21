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

A `main` ág pushainál és az oda nyitott PR-eknél ESP-IDF 5.5.4 build fut.

Sikeres build után artifactként elérhetők:

- `zolee-firmware-…`: OTA bináris, teljes USB ZIP, build-info és SHA256SUMS; az USB ZIP tartalmazza a bootloadert, partíciós táblát és flasher_args.json-t is
- `zolee-templates-…`: mind a kilenc sablon és a közös Loxone Home ZIP, mentett előnézetekkel
- `zolee-simulator-master-windows-<commit>`: fő kijelző Windows EXE, közös SD-erőforrásokkal
- `zolee-simulator-slave-4b-windows-<commit>`: Slave Windows EXE

A külön OTA-másolatok, Linux-szimulátor, PDB és nagy tesztképek feltöltése megszűnt;
a hozzájuk tartozó regressziós ellenőrzések megmaradtak.

A normál fejlesztői telepítéshez továbbra is az `idf.py flash` használata javasolt, mert automatikusan a megfelelő címekre írja az összes szükséges binárist.

## OTA frissítés

Az első USB-s telepítés után a későbbi firmware az eszköz helyi weboldalán is feltölthető:

`http://zolee-energy-flow-display.local/update`

OTA-hoz a build könyvtárból a fő alkalmazás bináris kell:

`build/zolee_energy_flow_display.bin`


## Előre fordított firmware

A GitHub release mellékletei és az automatikus build leírása: [releases.md](releases.md). Az USB-csomag magyar telepítési útmutatója: [usb-install.md](usb-install.md).

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

A kijelző LVGL konfigurációja `CONFIG_LV_OS_NONE=y`, `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1`. Régi helyi `sdkconfig` esetén ezeket is állítsd át a menuconfigban, majd konfiguráld és fordítsd újra a projektet. A firmware fordítási őrrel elutasítja a FreeRTOS OSAL/ több draw unit konfigurációt, hogy a régi beállítás ne kerülhessen észrevétlenül az új csomagba. Az ESP-IDF és az adapter továbbra is FreeRTOS-t használ.

Az SNTP automatikus SDK periódusa `CONFIG_LWIP_SNTP_UPDATE_DELAY=864000000` (10 nap). Régi helyi `sdkconfig` 14 napos értékét módosítsd az új értékre; a túl nagy időzítőt a fordítás elutasítja, mert az eszközön lwIP assertet és újraindulást okoz.
