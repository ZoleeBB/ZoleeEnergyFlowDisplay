# Felépítés

Az ESP32-P4 firmware közös állapotmodellt használ az MQTT- és HTTP-adatfogadáshoz. A kiválasztott fogadó ellenőrzi a teljes JSON-csomagot, majd az érvényes mintát a statisztika és a kijelző kapja meg. Hibás csomag nem módosít állapotot és nem hosszabbítja meg az adatfrissességet.

A konfiguráció a FAT32 microSD gyökerében található. A Wi-Fi csatlakozás után az NTP időszinkron szükséges a helyi naptári statisztikához. A fogadási módot a `DataReceptMode` választja; egyszerre egy mód aktív.

A LVGL 9.5.0 felület 1280×800 fekvő tájolású. Az állapotfrissítés 200 ms, az energiaanimáció időlépése 16 ms, a diagramok frissítése 5 másodperc. A képek gyorsítótára 6 MiB; a kijelző három teljes RGB565 képpufferrel és egy 50 soros LVGL rajzpufferrel működik. A PNG-k és a betűkészletek PSRAM-igényét a sablonvalidátor korlátozza.

A statisztika 35 helyi naptári napot tárol. A mai és tegnapi részletes adatok RAM-ban vannak; a pillanatfelvétel készítése és az SD-műveletek külön zárolást használnak. SD-hiba esetén az adatfogadás és a RAM-beli számolás folytatódik. A kártya állapotellenőrzése 5, újracsatlakoztatási próbája 60 másodpercenként történik.

A beépített nézet és nyolc SD-s sablon ugyanazt az adatmodellt használja. Balra/jobbra húzás lapoz, felfelé húzás nyitja a közös diagnosztikát, lefelé húzás bezárja. A webes OTA két alkalmazáspartíciót és automatikus visszaállítást használ.

Részletek: [adatprotokoll](mqtt.md), [statisztika](statistics.md), [sablonok](templates.md), [OTA](ota.md), [hibák](errors.md).

A MIPI LVGL rajzpuffer a PSRAM-ban van. A Waveshare BSP 2.0.3 belső RAM-ra rögzített beállítását a `main/app_display.c` regisztrációs linker-wrapper korrigálja. A függőségek eredeti forrásai érintetlenek; a méret, tájolás és frissítési mód megmarad.

Az adapter triple-partial pufferfoglalása is a PSRAM-beállítást követi: a CMake egy kizárólag buildkönyvtárbeli másolatot javít és fordít. Az eredeti managed component változatlan marad; ismeretlen vagy kétértelmű forrásváltozatnál a build megáll.

A MIPI-profil PPA gyorsítása tiltott. Az adapter 0.6.4 regisztrációja és két forgatási ága ezt eredetileg figyelmen kívül hagyja; a buildbeli `lvgl_bridge_v9.c` másolat a profilhoz köti a PPA használatát és a CPU út cache-visszaírását. A 90°-os forgatás így a meglévő CPU ágon fut, elkerülve az ESP32-P4 PPA részleges frissítésének dokumentált megakadását. Az SDK forrásait nem patch-eljük. A fizikai panelen a sebességet és az első kép megjelenését ellenőrizni kell.

Az LVGL `OS_NONE` módban egy szoftveres rajzolóegységet használ, ugyanúgy, mint a szimulátor. A FreeRTOS és az adapter LVGL-feladata továbbra is fut; az összes UI-hozzáférést az adapter recursive mutexe sorosítja. Külön LVGL OSAL rajzolószálak és azok task-notification szinkronizációja nincsenek. A szinkron PNG/vector rajzolás miatt az adapter feladat 32 KiB-os stackje PSRAM-ban van, hogy a belső RAM a DMA és a hálózat számára megmaradjon. A párhuzamos rajzolás elvesztésének teljesítményhatását fizikai panelen kell mérni.

A kijelző első render/flush eseményeinek egyszeri naplózása a regisztrációs wrapperben, az LVGL worker indítása előtt történik. Így az app_ui kezdeti zárolásának hibája nem akadályozza meg a renderelés és a panelre küldés elkülönítését. A fordítás tiltja az eltérő LVGL OS/render-worker konfigurációt.

Az lwIP automatikus SNTP periódusa 10 nap, az alkalmazás saját periódusa legfeljebb 7 nap (alapértelmezésben 6 óra). A 14 napos SDK érték az lwIP `UINT32_MAX/4` időzítőkorlátján kívül esett, ezért az új firmware fordítási őrrel is védi ezt a korlátot. A napi statisztika az első sikeres NTP-szinkron előtt nem indul el.
