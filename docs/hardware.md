# Hardver

A firmware célhardvere:

**Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1**

- SKU: 33150
- natív panel: 800 x 1280
- alkalmazás: fix 1280 x 800 fekvő tájolás
- ESP32-P4
- 32 MB NOR Flash
- 32 MB PSRAM
- Wi-Fi/Bluetooth társprocesszor
- MIPI-DSI kijelző
- microSD / TF foglalat
- USB Type-C programozás és hibakeresés

A projekt Waveshare BSP-je:

`waveshare/esp32_p4_wifi6_touch_lcd_x 2.0.3`

A kijelzőválasztás a projektben fixen a 10.1 colos panelre van állítva.

## Programozási mód

Normál esetben az ESP-IDF automatikusan kezeli a letöltési módba lépést az USB soros kapcsolaton.

Ha ez nem sikerül:

1. tartsd nyomva a **BOOT** gombot,
2. nyomd meg röviden az **RST** gombot,
3. engedd el a BOOT gombot,
4. indítsd újra a flash parancsot.

## Fontos

A firmware kifejezetten ehhez a 10.1 colos panelhez készül. Más Waveshare ESP32-P4 panelre ne töltsd fel változtatás nélkül.

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).
