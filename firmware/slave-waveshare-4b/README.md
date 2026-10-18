# Slave – Waveshare ESP32-P4-WIFI6-Touch-LCD-4B

Önálló firmware-cél, falra vagy asztali tartóra is szerelhető. Kizárólag BLE
Slave. A 10,1 colos image-et erre a panelre ne telepítsd.

| Azonosító | Érték |
| --- | --- |
| Termék / SKU | Waveshare ESP32-P4-WIFI6-Touch-LCD-4B / 31416 |
| Panel | 4 hüvelykes IPS, 720×720, ST7703, kétvonalas MIPI DSI |
| Érintés | GT911, I²C GPIO7/8, címpróba és polling a BSP-ben |
| Processzor / rádió | ESP32-P4NRW32 / ESP32-C6-MINI-1U-H8, SDIO |
| Memória | 32 MB flash, 32 MB PSRAM |
| Profil | ESP32-P4 rev3.x, 200 MHz PSRAM; ellenőrizd a leszállított chipet |
| Táp/programozás | USB-C USB–UART |
| Tárolás | NVS; SD-t behelyezve sem csatol |
| Projektazonosító | `zolee_slave_4b` |
| OTA asset | `ZoleeEnergyFlowDisplay-slave-4b-vX.Y.Z-ota.bin` |
| Partíciók | Két 12 MiB OTA slot, rollback |

Az ETH-2RO, a kerek 4C és az S3-as panelek eltérő hardverek.

## Fordítás és USB telepítés

ESP-IDF **5.5.4**, LVGL **9.5.0**, Waveshare 4B BSP **3.0.1**, LVGL adapter
**0.6.4**, ESP-Hosted **1.4.7**. A komponenseket a Component Manager tölti le.

```sh
idf.py -C firmware/slave-waveshare-4b set-target esp32p4
idf.py -C firmware/slave-waveshare-4b build
idf.py -C firmware/slave-waveshare-4b -p COM5 flash monitor
```

A COM5 helyére a tényleges port kell. Az első telepítés teljes USB flash:
az alkalmazásnév és a partíciós tábla különbözik a fő kijelzőétől.

A C6 rádió firmware-ét ez a projekt nem írja felül. ESP-Hosted 1.4.7-tel
kompatibilis C6 image szükséges; a gyári verziót a készüléken kell kipróbálni.
A fő kijelzőn bevált host verzió nem igazolja automatikusan az új C6 image-ét.
A kapcsolási rajz szerinti SDIO: CLK18, CMD19, D0=14, D1=15, D2=16, D3=17,
C6 reset=54. A BSP kezeli az ST7703 panelt és GT911 érintést.

## Működés

Automatikus masterkeresés. Első párosításkor a master diagnosztikáján megnyitott
párosítási ablak és az ott látható hatjegyű PIN kell. A PIN-pad a GT911 érintésén
működik; a bond és a védett Wi-Fi/OTA profil NVS-be kerül. A mastertől érkezik
az energia, időjárás, idő és időzóna.

Az örökölt Wi-Fi opcionális; a kijelző BLE-n internet nélkül is működik.
Az örökölt automatikus frissítés a közös napváltást használja. Az OTA választó
kizárólag a fenti külön assetnevet fogadja el, a letöltő projektazonosítót is
ellenőriz. A kiadási workflow mindkét célprogramot ugyanazzal a verzióval fordítja,
és külön OTA/USB csomagot készít. A közös SHA256SUMS.txt mindkét cél
ellenőrzőösszegeit tartalmazza. A 4B build eredete: slave-4b-build-info.json.

Az Energiaáramlás az egyetlen normál nézet. Az órára koppintás diagnosztikát
nyit/zár. Lejárt minta vagy kapcsolatvesztés esetén nincs régi teljesítményérték
vagy mozgás. Szigetüzemben a hálózati ág inaktív. A közös BLE kliens a master
statisztikai pillanatképét RAM-ba fogadja; nincs grafikon vagy helyi statisztikai fájl.

## Szimulátor

A firmware **ugyanazon** `ui/slave_4b_ui.c` fájlját, assetjeit és BLE kodekjét
fordítja. Windows: natív ablak, egérrel működő PIN-pad. Linux: képernyőképes
önellenőrzés. Nincs valódi rádiókapcsolat.

```sh
cmake -S firmware/slave-waveshare-4b -B out/slave-4b -DSLAVE_4B_SIMULATOR=ON
cmake --build out/slave-4b --config Debug --parallel 4
ctest --test-dir out/slave-4b -C Debug --output-on-failure
```

Windows: `out/slave-4b/simulator/Debug/zolee_slave_4b_simulator.exe`.
N: nappal/éjjel; fel/le: SOC; T: lejárt adat; R: újracsatlakozás; P: PIN-pad
(demó PIN: 123456). Az önellenőrzés a build simulator könyvtárába ír PPM képeket.

## Memória és közös kód

A BLE protokoll, SMP/PIN, NVS profil, időszinkron, Wi-Fi, RAM statisztikai cache,
napváltás és OTA közvetlenül a `main/` közös moduljaiból fordul, nem másolat.
A fő alkalmazás adatfogadó és sablonbeállításai itt nem szerepelnek.

A közös `cmake/display_psram.cmake` a pufferfoglalást, PPA-tiltást és
a részleges képkockák CPU-s javítását is átveszi. A `cmake/hosted_rx.cmake`
a bevált, korlátos SDIO DMA-ablakot és PSRAM RX-sort használja.
A közös `app_display.c` linker-wrapper a BSP rajzpuffereit PSRAM-ba irányítja,
PPA nélkül. Grafikus stack: 32 KiB PSRAM-ban; belső/DMA tartalék: 32 KiB.
Nincs 90/270 fokos forgatás. Három RGB565 panelpuffer összesen kb. 2,97 MiB.
PNG cache: 10 MiB. Két tömörített háttér: kb. 4 MiB flash. A dekódolt kép és
dekódolási átmeneti memória a 32 MiB PSRAM-ból foglal. A generátor csak beágyaz,
a PNG képeket nem módosítja.

## Hardveres átvételi próba

Ellenőrizendő: chip revízió, kép/érintés, háttérfény, első PIN, újraindítás utáni
bond, C6 kompatibilitás, BLE/Wi-Fi együtt, master kiesése, napváltás és 4B OTA
rollback. A szimulátor és a sikeres build nem helyettesíti ezeket.
Automata eredmények: ESP-IDF build (mindkét hardver és USB-csomag),
Waveshare 4B Slave workflow (Linux/Windows szimulátor).

[Rétegek](design/README.md) · [Terv](../../docs/slave-waveshare-4b.md)

Gyártói források:

- [Hardver](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4B)
- [BSP, példák, kapcsolási rajz](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4B)
