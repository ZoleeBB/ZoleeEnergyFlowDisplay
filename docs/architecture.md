# Felépítés

Az ESP32-P4 firmware közös állapotmodellt használ az MQTT-, HTTP-, közvetlen Loxone- és BLE Slave-adatfogadáshoz. Az első három út a közös JSON-validálón át publikál; a Slave a master bináris energia/időjárás/óra csomagjait ellenőrzi és állítja össze. Hibás vagy hiányos minta nem hosszabbítja meg az adatfrissességet. Az opcionális BLE master a már validált állapotot szolgáltatja; Slave módban nincs továbbosztás.

A konfiguráció SD-s működésben a FAT32 microSD gyökerében található. SD nélküli Slave-nél a `device_boot` NVS-névtér JSON-konfigurációt és tartós szerepjelölőt tárol. A `ble_profile` a párosított master Wi-Fi/OTA profilját őrzi. A helyi naptári statisztikához MQTT/HTTP/Loxone módban NTP, Slave módban a master UTC ideje és helyi UTC-eltolása szükséges. A fogadási módot a `DataReceptMode` választja; egyszerre egy mód aktív.

A LVGL 9.5.0 felület 1280×800 fekvő tájolású. Az állapotfrissítés 200 ms (az elfogadott navigációs gesztus a runtime taskot azonnal felébreszti), az energiaanimáció kerete 16 ms, a normál részleges frissítés periódusa 15 ms, kész frame utáni plusz várakozás nélkül, a diagramok frissítése 5 másodperc. A PNG-dekódolás gyorsítótára 10 MiB; a 10.1-es kijelző a három meglévő RGB565 panelpuffert használó `TRIPLE_PARTIAL` módban csak a változásokat rajzolja újra, és kész képeket ad át. A 4B saját, nem forgatott kijelzője továbbra is `TRIPLE_PARTIAL` módú. A PNG-k és a betűkészletek PSRAM-igényét a sablonvalidátor korlátozza.

A statisztika 35 helyi naptári napot tárol. A mai és tegnapi részletes adatok RAM-ban vannak; a pillanatfelvétel készítése és az SD-műveletek külön zárolást használnak. SD-hiba esetén az adatfogadás és a RAM-beli számolás folytatódik. A kártya állapotellenőrzése 5, újracsatlakoztatási próbája 60 másodpercenként történik.

Ez az SD-s Slave saját integrálására is érvényes; az összes telepített témát
használhatja. SD nélkül a helyi integrálás és az SD-helyreállító task kimarad,
csak a beépített Rendszer nézet aktív. A 1005 GATT-karakterisztikán érkező,
6664 bájtos master-pillanatkép 14 lapon, ellenőrzés után atomi RAM-cserével frissül.
Az élő energia/óra értesítések ettől külön működnek. A 1004 profil és a 1005
statisztika LE Secure Connections/MITM/bonding és 16 bájtos kulcs mellett érhető el.
Az NVS-írást külön profil-worker végzi, a statisztikai GATT-kiszolgáló nem végez SD I/O-t.

A témaváltáskor a betöltőképernyő mögött felszabadul az előző téma dekódolt
képgyorsítótára, az SD-olvasás és a felépítés pedig az előre dekódolt
kép-segédfájlokból dolgozik, ha azok elkészültek. A segédfájlokat külön,
alacsony prioritású háttérfeladat (`template_raw`) állítja elő fájlonként, a
katalógus- és SD-zárat csak addig tartva; a lapozás, a telepítés és a webes
feltöltés elsőbbséget élvez. Részletek: [teljesítmény](performance.md).

A beépített nézet és tíz SD-s sablon ugyanazt az adatmodellt használja. Balra/jobbra húzás témát vált; Master/Loxone Otthon-nézetben a saját oldalak között lapoz. Felfelé húzás nyitja a diagnosztikát, a jobb felső X zárja. Master/Loxone módban lefelé húzás az Otthon-oldalakat, a ház érintése a fogyasztókat nyitja. A webes OTA két alkalmazáspartíciót és automatikus visszaállítást használ.

Részletek: [adatprotokoll](mqtt.md), [statisztika](statistics.md), [sablonok](templates.md), [OTA](ota.md), [hibák](errors.md).

A MIPI LVGL képpufferei PSRAM-ban vannak. A Waveshare BSP 2.0.3 belső RAM-ra rögzített beállítását a `main/app_display.c` regisztrációs linker-wrapper korrigálja. A függőségek eredeti forrásai érintetlenek; a méret és tájolás megmarad. A v3 Master a panel meglévő három pufferét és egy 800×50 pixeles, 80 000 bájtos PSRAM-részpuffert használ.

A 4B-n továbbra is használt triple-partial pufferfoglalás is a PSRAM-beállítást követi: a CMake egy kizárólag buildkönyvtárbeli másolatot javít és fordít. Az eredeti managed component változatlan marad; ismeretlen vagy kétértelmű forrásváltozatnál a build megáll.

A MIPI-profil PPA gyorsítása tiltott. Az adapter 0.6.4 regisztrációja és két forgatási ága ezt eredetileg figyelmen kívül hagyja; a buildbeli `lvgl_bridge_v9.c` másolat a profilhoz köti a PPA használatát és a CPU út cache-visszaírását. A 270°-os forgatás így a meglévő CPU ágon fut, elkerülve az ESP32-P4 PPA részleges frissítésének dokumentált megakadását. Az SDK forrásait nem patch-eljük. A fizikai panelen a sebességet és az első kép megjelenését ellenőrizni kell.

Az LVGL `OS_NONE` módban egy szoftveres rajzolóegységet használ, ugyanúgy, mint a szimulátor. A FreeRTOS és az adapter LVGL-feladata továbbra is fut; az összes UI-hozzáférést az adapter recursive mutexe sorosítja. Külön LVGL OSAL rajzolószálak és azok task-notification szinkronizációja nincsenek. A szinkron PNG/vector rajzolás miatt az adapter feladat 32 KiB-os stackje PSRAM-ban van, hogy a belső RAM a DMA és a hálózat számára megmaradjon. A párhuzamos rajzolás elvesztésének teljesítményhatását fizikai panelen kell mérni.

A kijelző első render/flush eseményeinek egyszeri naplózása a regisztrációs wrapperben, az LVGL worker indítása előtt történik. Így az app_ui kezdeti zárolásának hibája nem akadályozza meg a renderelés és a panelre küldés elkülönítését. A fordítás tiltja az eltérő LVGL OS/render-worker konfigurációt.

Az lwIP automatikus SNTP periódusa 10 nap, az alkalmazás saját periódusa legfeljebb 7 nap (alapértelmezésben 6 óra). A 14 napos SDK érték az lwIP `UINT32_MAX/4` időzítőkorlátján kívül esett, ezért az új firmware fordítási őrrel is védi ezt a korlátot. A napi statisztika az első sikeres NTP- vagy Slave esetén BT-időszinkron előtt nem indul el.

Az F14 Master CPU-útja minden frame első részlete előtt a legutóbb leadott
kész képet másolja egy szabad hátsó panelpufferbe. Erre csak a változott
LVGL-részleteket forgatja; az utolsó részlet után egyszer írja vissza a CPU-
cache-t és adja át a teljes kész képet. Nincs diff-javítás vagy CPU/DMA által
közösen írt cache-vonal, és nem módosul az LVGL invalidációs téglalaplistája.
A 4B nem forgatott részleges útja változatlan. A grafikus/touch feladat CPU1-en,
a Loxone worker CPU0-n fut; a közös állapotot az eddigi mutexek védik.
Home-belépéskor az előző nézet és a globális képcache felszabadul. Az aktív
oldal PNG-i egyszer, megjelenítés előtt dekódolódnak: RGB565 háttér (2 048 000
bájt), ARGB logó/ikonok és a fogyasztói ikonok szürke másolatai. Dekódolási
hiba teljes előtöltési visszavonást és korlátos újrapróbálást eredményez,
rajzolás közben nincs PNG-dekódolás vagy ideiglenes szürke snapshot.

A címkék csak eltérő szövegnél kapnak új tartalmat. A gyakran frissülő színek és opacity értékek saját lokális stílusérték esetén is csak változáskor íródnak; az örökölt egyezés nem helyettesíti az explicit lokális beállítást. Ezzel csökken a 200 ms-os állapotfrissítés memóriaművelete és rajzolási terhelése. Ugyanez a szabály érvényes a 4B Slave 33 ms-os frissítésére és a kaputelefon-fedőrétegre: változatlan adatnál a ciklus egyetlen területet sem érvénytelenít (ezt a szimulátor és a preview teszt számlálja). A diagnosztikai pillanatkép (heap-statisztika, Wi-Fi RSSI) csak a megnyitott diagnosztikai nézethez frissül. Az animáció a valódi eltelt idővel lép; a 7 kW-os sebességkorlát megmarad. A touch külön, eredeti időzítője megmarad. A gesztus teljes elmozdulását az ismételt koordináták sem törlik; az 50 px-es távolságküszöb kiszűri a kis remegést.

Az ESP-Hosted 1.4.7 SDIO streaming RX driverének buildbeli másolata a DMA után készülő CPU-csomagmásolatokat PSRAM-ban tárolja. A hozzájuk rendelt felszabadító callback heap_caps_free; a hardveres streaming RX DMA egy fix 1 KiB-os, igazított belső ablakot használ. A teljes stream PSRAM-ban van, részolvasásonként CPU-másolattal töltjük; foglalási hibánál a FIFO kiürül és az adat eldobódik. A foglalási hiba csomageldobást vált ki, a hálózati payload másolatának hiánya is felszabadítással kezelhető. A patch forrásalak-ellenőrzése és a foglalási hibát injektáló teszt védi ezt az illesztést.

A generikus UI/JSON/zlib heap foglalások PSRAM-ot részesítenek előnyben, 32 KiB védett belső/DMA tartalékkal. A 10 MiB LVGL képcache a legnagyobb jelenet kb. 8,2 MiB dekódolt képkészletét befogadja. A worker build-másolata a mutex elengedése után egy valódi tickre blokkol; a pending értesítések így nem tudják a pihenőfeladatot korlátlanul kiszorítani. A hálózati TX pufferhiánya visszaadott hibakód, a watchdog továbbra is aktív.

A sablon-geometria külső BSS-ben, PSRAM-ban van; a skálázott képek legfeljebb 6 MiB-os, témaváltáskor felszabadított natív raster-cache-t használnak. A memória- és képfrissítési döntések, kutatási források és hardveres mérés részletei: [teljesítmény](performance.md).

A diagnosztika bal oldalán két oszlopban 14 adatcsoport, jobb oldalán a meglévő
RAM-eseménynapló legfrissebb 15 bejegyzése látszik, legújabb felül. A
figyelmeztetések narancssárgák, a hibák pirosak. A naplómásolat statikus
memóriában él; a másolás az LVGL-lock előtt, csak nyitott diagnosztikánál
történik. Ez nem ír új naplófájlt az SD-re.

A helyi napváltás a statisztikai karbantartásból ébreszti az opcionális GitHub
OTA-feladatot. A kézi OTA, konfigurációmentés és szabályozott újraindítás közös
kizárást használ. A próbaindítás validációja SD nélküli Slave-nél friss master-
statisztikát kér az SD helyett; a többi mód feltételei az [OTA útmutatóban](ota.md)
szerepelnek.

A Loxone kliens RSA/AES-hitelesítést, JWT-megújítást, állapotfelderítést és WebSocket-eseménytáblákat kezel. A fájlkérés 404-es hibájánál HTTP(S) token-HMAC-hitelesítéssel tölti le a struktúrát. Csak a párosított számok állapotát őrzi, öt másodpercenként a közös validálón át publikál mintát. A külön SD-konfiguráció eszközkulccsal védi a jelszót. [Részletek](loxone-direct.md).

A Master navigációját az input `LV_EVENT_GESTURE` eseménye vezérli. A kötés
inputonként egyszeri; az ismételt nézetépítés nem sokszoroz callbacket.
A fix képernyő nem görgethető. Párosítás, diagnosztika és betöltés alatt az
energiatéma gesztusai tiltottak. A runtime az adapter saját LVGL-feladatának kész képkockáját várja meg az SD-/PNG-munka előtt, a várakozás alatt elengedi a GUI-lockot; input callback
csak állapotkérést állít. A szimulátor ugyanazt az előkészítési sorrendet követi.
