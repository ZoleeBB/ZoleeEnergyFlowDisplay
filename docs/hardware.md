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

## Waveshare ESP32-P4-WIFI6-Touch-LCD-4B (Slave)

- natív panel: 720 × 720, 4 colos IPS, MIPI-DSI
- BSP: `waveshare/esp32_p4_wifi6_touch_lcd_4b 3.0.1`
- érintés: GT911, I2C (GPIO7/8), INT és RST nélkül, pollozva
- háttérvilágítás: GPIO26 PWM (5 kHz, invertált) + GPIO33 BL_EN (boost engedély)
- nincs SD-kártya: a beállításokat és a fényerőt a Mastertől kapja

### Két Type-C csatlakozó – melyik mire való

| Port | Mire megy | Mit lát a gép |
| --- | --- | --- |
| **USB OTG** | közvetlenül az ESP32-P4 nagy sebességű USB-jére | **semmit**: a firmware nem inicializálja |
| **USB TO UART** | CH343P átalakító → P4 UART0 (GPIO37/38) | soros port: táp, flashelés és **napló** |

A soros konzol tehát külön beállítás nélkül, 115200 baudon elérhető – de csak a
**USB TO UART** porton. Ezen látszik az indulási napló, a pánik-visszakövetés, a
BSP fényerősorai (`Setting LCD backlight: …`) és – a v3.3.5-től – az
eseménynapló minden bejegyzése is.

```
py -m serial.tools.miniterm COM4 115200 --raw
```

A USB-Serial-JTAG (GPIO24/25) a panelon nincs csatlakozóra vezetve, ezért azon
nem jelenik meg port.

### A háttérvilágítás alsó korlátja

A boost meghajtó **48% alatt kialszik** (48%-on világít, 47%-on sötét), ezért a
Slave firmware a kért 1–100%-ot a 48–100%-os valódi tartományra skálázza, a 0%
pedig marad igazi teljes sötét. Részletek:
[Waveshare 4B](slave-waveshare-4b.md).

## Fontos

A Master firmware kifejezetten ehhez a 10.1 colos panelhez készül. Más Waveshare ESP32-P4 panelre ne töltsd fel változtatás nélkül.

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

Az érintési koordináták a fekvő képhez igazodnak: a driver natív Y-tükrözés után X/Y tengelycserét végez. A vízszintes húzás témát vált; felfelé húzás a diagnosztikát nyitja, a jobb felső X zárja. Master/Loxone módban lefelé húzás az Otthon-oldalaké. A 270°-os képforgatást és ezeket az érintési flag-eket együtt kell kezelni.

A 270°-os fekvő tájolás a kamera alul elhelyezkedő felszereléséhez igazodik. Az érintés natív Y-tükrözéssel és tengelycserével ugyanazt a tájolást követi.
