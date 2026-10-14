# Felépítés

Az ESP32-P4 firmware közös állapotmodellt használ az MQTT- és HTTP-adatfogadáshoz. A kiválasztott fogadó ellenőrzi a teljes JSON-csomagot, majd az érvényes mintát a statisztika és a kijelző kapja meg. Hibás csomag nem módosít állapotot és nem hosszabbítja meg az adatfrissességet.

A konfiguráció a FAT32 microSD gyökerében található. A Wi-Fi csatlakozás után az NTP időszinkron szükséges a helyi naptári statisztikához. A fogadási módot a `DataReceptMode` választja; egyszerre egy mód aktív.

A LVGL 9.5.0 felület 1280×800 fekvő tájolású. Az állapotfrissítés 200 ms, az energiaanimáció időlépése 16 ms, a diagramok frissítése 5 másodperc. A képek gyorsítótára 6 MiB; a kijelző három részleges RGB565 pufferrel működik. A PNG-k és a betűkészletek PSRAM-igényét a sablonvalidátor korlátozza.

A statisztika 35 helyi naptári napot tárol. A mai és tegnapi részletes adatok RAM-ban vannak; a pillanatfelvétel készítése és az SD-műveletek külön zárolást használnak. SD-hiba esetén az adatfogadás és a RAM-beli számolás folytatódik. A kártya állapotellenőrzése 5, újracsatlakoztatási próbája 60 másodpercenként történik.

A beépített nézet és nyolc SD-s sablon ugyanazt az adatmodellt használja. Balra/jobbra húzás lapoz, felfelé húzás nyitja a közös diagnosztikát, lefelé húzás bezárja. A webes OTA két alkalmazáspartíciót és automatikus visszaállítást használ.

Részletek: [adatprotokoll](mqtt.md), [statisztika](statistics.md), [sablonok](templates.md), [OTA](ota.md), [hibák](errors.md).

A MIPI LVGL rajzpuffer a PSRAM-ban van. A Waveshare BSP 2.0.3 belső RAM-ra rögzített beállítását a `main/app_display.c` regisztrációs linker-wrapper korrigálja. A függőségek eredeti forrásai érintetlenek; a méret, tájolás és frissítési mód megmarad.
