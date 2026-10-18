# Desk Slave – Waveshare ESP32-P4-WIFI6-Touch-LCD-4B

Állapot: látványtervezés és hardver-előkészítés. A 720×720-as firmware még nem
fordítható és nem telepíthető; a jelenlegi 10,1 colos image-et erre ne használd.

Ez a könyvtár a különálló, kizárólag Slave szerepű asztali alkalmazás helye.
A fejlesztési ág: `feature/desk-slave-waveshare-4b`, kiindulása a 14-es PR-t
egyesítő `86d0b326a04e4e32cc44bc223c405a2eef8e0093` main-commit.

| Azonosító | Cél |
| --- | --- |
| Termék | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B |
| Gyártói SKU | 31416 |
| Tervezett firmware-célazonosító | `desk-slave-waveshare-4b` |
| Panel | 4 hüvelykes négyzetes IPS, 720×720 |
| Kijelzővezérlő és kapcsolat | ST7703, két adatvonalas MIPI DSI |
| Érintés | GT911, kapacitív, I²C |
| Fő processzor | ESP32-P4NRW32 |
| Rádiós társprocesszor | ESP32-C6-MINI-1U-H8, Wi-Fi 6 és BLE, SDIO |
| Memória | 32 MB flash és 32 MB PSRAM, gyártói specifikáció szerint |
| Táp/programozás | USB-C USB–UART csatlakozó |
| Futási szerep | Csak BLE Slave, SD nélkül, belső NVS-beállításokkal |

Az ETH-2RO (31570), a kerek 4C és az ESP32-S3-as 480×480-as eszköz más hardver.
A leszállított példány PCB-verzióját és C6-firmware-ét az első eszközös próbán
rögzíteni kell.

Részletes megjelenési és működési terv: [Desk Slave terv](../../docs/desk-slave-waveshare-4b.md).
A [design könyvtár](design/README.md) a képi rétegek elkülönítését dokumentálja.

Gyártói források:

- [Hardver és GPIO-kiosztás](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B)
- [Hivatalos példák és ST7703/GT911 board support](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B)

A protokoll, párosítás, NVS-profil és időszinkron a már elkészült közös kódból
származik majd. Az alkalmazás saját projektazonosítót, buildet és OTA-assetet kap;
a meglévő 10,1 colos alkalmazás külön firmware-cél marad. Ebben az első lépésben
nem változik a jelenlegi master firmware-kódja vagy kiadási folyamata.
