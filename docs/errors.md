# Hibakezelés

## Induláskori blokkoló hibák

Az alábbi hibák esetén a normál dashboard nem indul el:

- SD-kártya nem érhető el
- config.json hiányzik
- config.json hibás
- Wi-Fi inicializálás sikertelen
- NTP inicializálás sikertelen
- statisztikai tárhely nem inicializálható
- HTTP módban a fogadó webszerver nem indítható
- szükséges háttérfeladat nem indítható

A képernyőn a konkrét hiba látható, a részletes technikai log a soros konzolon érhető el.

## Wi-Fi kapcsolat megszakad

Azonnal teljes képernyős `Nincs Wi-Fi kapcsolat` állapot jelenik meg.

Újracsatlakozás alapértelmezés szerint 15 másodpercenként történik. Siker esetén a normál működés automatikusan folytatódik.

## MQTT kapcsolat megszakad

Külön MQTT kapcsolati hibaképernyő jelenik meg. Az újracsatlakozási idő alapértelmezés szerint 15 másodperc.

## Adatkimaradás

Ha az aktív MQTT- vagy HTTP-fogadó kész, de a konfigurált időn belül nincs érvényes csomag, teljes képernyős adathiányjelzés jelenik meg. A hibás csomag nem indítja újra az időkorlátot; érvényes csomagnál a felület automatikusan helyreáll.

## Futás közbeni SD hiba

A dashboard és az adatfeldolgozás tovább működik RAM-ból.

A képernyőn piros `SD-kártya hiba` figyelmeztetés jelenik meg. A firmware 1 percenként megpróbálja helyreállítani az SD kapcsolatot.

## Status = 4

Ez nem kommunikációs hiba. A dashboard a helyén marad, a rendszerállapot kártya erős piros jelzést kap.

## Watchdog

A firmware task watchdogot használ. A soros logban a reset oka is megjelenik, és a helyi információs weboldalon is lekérdezhető.

## RAM eseménynapló

A legutóbbi fontos események és hibák RAM-ban tárolódnak és a helyi webes információs oldalon megjelennek. Folyamatos SD logolás nincs.

MQTT indítási hiba a kommunikációs állapotban látszik; nem azonos a rendszer végzetes inicializálási hibájával. Az időjárási `null` érték nem hiba, csak a kapcsolódó kijelzés marad rejtve.

## Fekete képernyő és újraindulás a kijelző indításakor

A `Display initialized` után következő `alloc partial draw buffer 80000 bytes failed`, majd `bsp_display_start_with_config` assert a belső RAM-ból történő rajzpuffer-foglalás hibája. A 32 MB PSRAM ettől még működhet. A firmware a Waveshare BSP 2.0.3 MIPI regisztrációját PSRAM-pufferre állítja; induláskor `app_display: MIPI LVGL draw buffers use PSRAM` naplósor jelzi ezt. A panel három képpuffere, az 50 soros rajzpuffer és a fekvő tájolás megmarad. A managed component forrásfájljait nem módosítja.

A régi firmware a soros konzolon ismétlődően újraindulhat; ehhez a javításhoz az új firmware teljes USB-csomagját kell feltölteni. A telepítő Python-fájljának cseréje önmagában nem változtatja meg a már feltöltött alkalmazást.

Az adapter triple-partial pufferfoglalása is a PSRAM-beállítást követi: a CMake egy kizárólag buildkönyvtárbeli másolatot javít és fordít. Az eredeti managed component változatlan marad; ismeretlen vagy kétértelmű forrásváltozatnál a build megáll.

## Háttérvilágítás van, az LVGL indulása után fekete a képernyő

Az adapter 0.6.4 a `profile.enable_ppa_accel=false` beállítás ellenére PPA-val forgathat. Az Espressif az ESP32-P4 + triple-partial + 90°/270° kombinációhoz [PPA megakadást dokumentál](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html#ppa-freeze-fix-esp32-p4). A firmware a profil tiltását a regisztrációnál és mindkét forgatási ágon érvényesíti, és a meglévő CPU forgatást használja. A build kizárólag másolatot módosít; az SDK és a managed component eredetije érintetlen.

Az új logban `MIPI rotation uses CPU fallback (PPA disabled)` jelzi a beállítást. Ezt követi a kijelző zárolása, a bootképernyő létrehozása, az SD csatolása és a konfiguráció betöltése. A `First LVGL refresh completed` az első LVGL rajzolási ciklus végét jelzi; önmagában nem bizonyítja a fizikai panel képét. A legelső LVGL zárolás 5 másodperces időkorlátot kapott, ezért a korai megakadás konkrét hibasorként jelenik meg.

Az `on_frame_buf_complete unavailable` figyelmeztetés önmagában nem bizonyít callback-hibát: ezen a verzión az adapter `on_refresh_done` tartalék callbacket is regisztrál. SD- vagy konfigurációs hiba normál esetben hibaképernyőt ad, ezért az első rajzolás nélküli fekete képet először a kijelző útvonalán vizsgáljuk. Ha a jelenség megmarad, a RESET-től induló teljes soros log szükséges, az új indulási bejegyzésekkel együtt.

### LVGL indul, de az első zárolás öt másodperc után lejár

A `Display started; waiting for LVGL lock` után következő `Initial LVGL lock failed: ESP_ERR_TIMEOUT` azt jelzi, hogy az adapter LVGL-feladata még fogva tartja a mutexet. Ez a log önmagában nem dönti el, hogy a szoftveres rajzolás vagy a panelpufferek várakozása akadt el. A PPA tiltása mellett is előfordult, ezért nem kezeljük kizárólag PPA-hibaként.

A firmware az LVGL két FreeRTOS-os rajzolószála helyett az adapter feladatán futó, soros renderelést használ (`OS_NONE`, egy draw unit); az adapter zárolása megmarad. Az LVGL feladat 32 KiB-os PSRAM stackjén fut a PNG/vector rajzolás. Hasonló ESP32-P4 / LVGL 9.5.0 [OSAL megakadásról az upstream hibajegy is beszámol](https://github.com/lvgl/lvgl/issues/10349), de az adott eszköz pontos elakadását a további eseménylog és a hardveres próba dönti el.

Az induláskor egyszer megjelenő sorok:

- `LVGL rendering is synchronous (OS_NONE, one draw unit)`: az új konfiguráció jelzése.
- `First LVGL render started`: a rajzolási ciklus elkezdődött.
- `First LCD flush started`: az első rajzpuffert átadjuk az adapternek.
- `First LCD flush callback returned`: az adapter első flush hívása visszatért; ez nem bizonyítja a fizikai képet.
- `First adapter refresh completed`: az első teljes LVGL frissítési ciklus véget ért.

Ezeket a callbackeket az LVGL-feladat indítása előtt regisztráljuk, ezért zárolási timeout esetén is látszanak. Ha a renderelés kezdete megvan, de flush nincs, a rajzolási út állt meg. Ha flush indul, de nem tér vissza, az adapter/panel út várakozását kell vizsgálni. Mindig a RESET-től induló teljes logot őrizd meg. A COM-port a reset alatt rövid időre eltűnhet; az egyszeri `FileNotFoundError` és az utána sikeres megnyitás nem az LVGL zárolási hibája.

## Újraindulás az időszinkron indításakor: sys_timeout assert

A `Timeout time too long, max is LWIP_UINT32_MAX/4 msecs` assert firmware-időzítési hiba. A korábbi automatikus SNTP-periódus 14 nap (1 209 600 000 ms) volt, ami meghaladja az lwIP 1 073 741 823 ms-os (~12,4 napos) felső korlátját. A javított érték 10 nap (864 000 000 ms), így hosszabb az alkalmazás legfeljebb 7 napos kézi újraszinkron-periódusánál, de belefér az lwIP korlátjába. Fordítási őr tiltja a túl nagy értéket, régi helyi sdkconfig esetén is.

Ez az assert nem bizonyít NAS- vagy hálózati hibát. Az NTP-szerver lehet közvetlen IP-cím is. Az új napló jelzi az inicializálást, a szervert, a tíz másodperces várakozást és annak eredményét. Az indulás továbbra is megvárja a sikeres időszinkront, mert a napi statisztika valós helyi időt igényel; sikertelen válasznál a konfigurált retry szerint próbálkozik újra.

A kezdeti Wi-Fi/MQTT kapcsolódás a bootképernyőn marad. A kapcsolatvesztés hibaképernyője az adott indulási kapcsolódási lépés után használható. A hibaikon háttérkörén csak a színes körvonal átlátszósága pulzál; a PNG-t tartalmazó teljes objektum átlátszósága változatlan, és nincs örökölt outline/shadow.
