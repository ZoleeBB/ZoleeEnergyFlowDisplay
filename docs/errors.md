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

Ha az aktív MQTT-, HTTP- vagy Loxone-fogadó kész, de a konfigurált időn belül nincs érvényes csomag, teljes képernyős adathiányjelzés jelenik meg. A hibás csomag nem indítja újra az időkorlátot; érvényes csomagnál a felület automatikusan helyreáll.

## Futás közbeni SD hiba

A dashboard és az adatfeldolgozás tovább működik RAM-ból.

A képernyőn piros `SD-kártya hiba` figyelmeztetés jelenik meg. A firmware 1 percenként megpróbálja helyreállítani az SD kapcsolatot.

## Status = 4

Ez nem kommunikációs hiba. A dashboard a helyén marad, a rendszerállapot kártya erős piros jelzést kap.

## Status = 5

**Hálózat újra elérhető**: a hálózati feszültség visszatért. HTTP, MQTT,
közvetlen Loxone és Master–Slave módban is érvényes állapot; nem kommunikációs
hiba, és önmagában nem vált újracsatlakozási képernyőre. A Nap pálya v21
nagyobb, zöld felirattal és nappali/éjszakai kontrasztos buborékkal jelzi.
Az 5-ös érték használata előtt minden fogadó firmware-ét frissíteni kell:
[státuszok és verziókompatibilitás](mqtt.md#status).

## Watchdog

A firmware task watchdogot használ. A soros logban a reset oka is megjelenik, és a helyi információs weboldalon is lekérdezhető.

## RAM eseménynapló

A legutóbbi fontos események és hibák RAM-ban tárolódnak és a helyi webes információs oldalon megjelennek. Folyamatos SD logolás nincs.

MQTT indítási hiba a kommunikációs állapotban látszik; nem azonos a rendszer végzetes inicializálási hibájával. Az időjárási `null` érték nem hiba, csak a kapcsolódó kijelzés marad rejtve.

## Fekete képernyő és újraindulás a kijelző indításakor

A `Display initialized` után következő `alloc partial draw buffer 80000 bytes failed`, majd `bsp_display_start_with_config` assert a belső RAM-ból történő rajzpuffer-foglalás hibája. A 32 MB PSRAM ettől még működhet. A firmware a Waveshare BSP 2.0.3 MIPI regisztrációját PSRAM-pufferre állítja; induláskor `app_display: MIPI LVGL draw buffers use PSRAM` naplósor jelzi ezt. A Master a három meglévő panelpuffert teljes képkockás `TRIPLE_FULL` módban használja; külön 50 soros részleges rajzpuffer nincs. A fekvő tájolás megmarad. A managed component forrásfájljait nem módosítja.

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

## Villódzó vagy függőleges csíkokra széteső feliratok

A feliratterületeken és a diagnosztikában is megjelenő képtorzulás nem csak animációs stílushiba lehet. A CPU forgatás és a DMA2D back-buffer javítás megoszthat egy cache-vonalat: az utólagos CPU write-back felülírhat DMA-val visszamásolt szomszédos képpontokat. A korábbi részleges út CPU forgatás mellett CPU-val javítja a részleges puffert is, megőrzi a kész kép write-backjét és kihagyja az ezen az úton szükségtelen invalidálást. Ezt a `Partial framebuffer repair uses CPU; DMA cache-line mixing disabled` sor jelzi. A Master aktuális `TRIPLE_FULL` útja ezt a részleges javítást már nem használja.

A változatlan szövegek, színek és átlátszóságok nem indítanak ismételt újrarajzolást. A forgatás, az animáció sebességképlete és a részleges frissítési mód megmarad. A tényleges villódzás megszűnését és a folyamatosságot a fizikai panelen kell ellenőrizni.

## Vízszintes húzás témaváltás helyett diagnosztikát nyit

A kijelző képe 270°-kal forgatott, az érintőpanel natív koordinátái 800×1280-asak. A firmware az érintődriverben először tükrözi a natív Y-t, majd felcseréli az X/Y tengelyeket (`mirror_x=0`, `swap_xy=1`, `mirror_y=1`). Így a vízszintes húzás lapoz, a függőleges húzás a diagnosztikát kezeli. A többujjas `LV_USE_GESTURE_RECOGNITION` modul kikapcsolásáról szóló figyelmeztetés nem tiltja a hagyományos egyujjas pointer-gesztusokat; a lapozás azokkal működik.


## Újraindulás weboldal megnyitásakor: SDIO RX puffer

Az `sdio_push_data_to_queue ... (pkt_rxbuff)` assert az ESP-Hosted 1.4.7 streaming fogadóútján sikertelen csomagpuffer-foglalást jelez. Ez belső DMA-képes memóriát igényel; a kijelzett összes szabad heap a PSRAM-ot is tartalmazza, ezért önmagában nem zárja ki ezt a hibát.

A buildbeli driver-másolat a DMA után készülő, kizárólag CPU által használt csomagmásolatot PSRAM-ba teszi, hozzá illő felszabadító callbackkel. A streaming RX közvetlen DMA-ja egy fix 1 KiB-os belső ablakot használ; a teljes stream PSRAM-ban van. A csomaghosszt foglalás előtt ellenőrzi; csomagmásolat vagy hálózati payload foglalási hibájánál felszabadít és eldob ahelyett, hogy assert miatt újraindulna. Pufferhiánykor `RX packet copy unavailable` vagy `Network RX payload unavailable` naplósor jelenik meg. A TCP újraküldhet, UDP adat elveszhet; a hálózati terhelés alatti működést az eszközön is ellenőrizni kell.

A javítás az ESP-Hosted rögzített 1.4.7 forrásalakjára készül, és eltérő függőség esetén megállítja a buildet. Az SDK és a managed component eredetije nem változik.


## LVGL watchdog és SD/hálózati memóriahiány témaváltáskor

Az `IDLE0` watchdog és `CPU 0: lvgl` ismétlődő naplósorok a renderelési feladat CPU-éheztetését jelzik. A flush értesítése miatt az adapter `ulTaskNotifyTake` hívása várakozás nélkül visszatérhet. A buildbeli worker-másolat minden renderelési ciklus végén, a grafikus mutex elengedése után egy valódi FreeRTOS tickre blokkol. A watchdog engedélyezve marad, 60 másodperces időkorláttal.

Az Élő otthon világos jelenetének teljes hivatkozott PNG-készlete dekódolva kb. 8,2 MiB; a korábbi 6 MiB cache elégtelen volt. A firmware 10 MiB cache-t használ. A teljes hivatkozott képkészlet alapján ellenőrzött regresszió védi a keretet; az aktuális képek újrafelhasználhatók ismételt dekódolás helyett.

Az általános malloc foglalások PSRAM-ot részesítenek előnyben (`SPIRAM_MALLOC_ALWAYSINTERNAL=0`), a kötelező belső/DMA foglalásoknak 32 KiB belső tartalék marad. Az `sdmmc_read_sectors: not enough mem` és `transport_drv_sta_tx ... (copy_buff)` a belső memória hiányára utalnak. Az SDIO TX DMA-puffereit nem helyezzük PSRAM-ba; a STA/AP TX pufferfoglalás hibája hálózati hibakódot ad vissza assert helyett.


## Újraindulás még az alkalmazás indítása előtt: DMA-tartalék

A `Reserving pool of 64K ...`, majd `Could not reserve internal/DMA pool (error 0x101)` és `abort()` azt jelzi, hogy az ESP-IDF az app_main előtt nem tudja lefoglalni a konfigurált belső tartalékot. A PSRAM memóriaellenőrzése ettől még sikeres lehet; a hiba a belső memóriára vonatkozik. A korábbi 64 KiB-os növelés regresszió volt, a javított konfiguráció visszatér a hardveren már elinduló 32 KiB-os beállításhoz. A generikus foglalások PSRAM-előnyben részesítése, a 10 MiB cache és a worker blokkoló tickje megmarad.

A build megtagadja a 32 KiB feletti belső tartalékot ezen a rögzített Waveshare P4 célon, és regressziós ellenőrzés védi a korlátot. A fordítás sikere önmagában nem jelent hardveres indulási próbát.

## `sdio_rx_get_buffer ... (*buf)` témaváltás vagy webkérés közben

A változó méretű streaming RX DMA-pufferhez nem maradt megfelelő összefüggő belső blokk. A javítás fix 1 KiB DMA-ablakot és PSRAM stream-tárolót használ. PSRAM-hiánynál `RX stream storage unavailable ... draining stream` üzenet jelzi az eldobást; a FIFO számláló frissül, a későbbi adatok fogadhatók. A diagnosztika `free_internal_kib` és `largest_dma_block_bytes` értéke segít megkülönböztetni a teljes memóriahiányt a DMA-fragmentációtól. Lásd a [kutatást és ellenőrzéseket](performance.md).

## Túl gyakori sikeres NTP-szinkron

Ha 6 órás beállítás mellett körülbelül 125 másodpercenként látszik sikeres
szinkron, a korábbi firmware 32 bites ms×Hz konverziója túlcsordult.
A várakozás most 64 biten számolódik; siker után a konfigurált órás periódus,
hibánál vagy Wi-Fi-hiánynál a konfigurált perces újrapróbálás érvényes.
A regresszió a valódi háttértaskot futtatja szimulált várakozással,
6/168 órás periódussal, 100/1000 Hz-en és a helyreállási ágakon is.

## Közvetlen Loxone kapcsolat

A [kaputelefon](intercom.md) működő videónál és létrejött beszélgetésnél nem
mutat folyamatos diagnosztikai adatokat. Kapcsolódáskor rövid folyamatjelzés,
hibánál hitelesítési/hálózati/kép-/hanghardver-üzenet jelenik meg. Ha tíz
másodpercig nincs elfogadott bejövő hangcsomag, a beszélgetés bontódik;
a részletes RTP-számlálók a soros naplóba kerülnek. A webes csengőhangminta
a böngészőben szólal meg. Hiányzó vagy hibás SD-hangfájlnál a kijelző
érthető hibaüzenetet mutat.

Hiányzó paramétereknél külön fekete Loxone-beállítóképernyő jelenik meg. A kezdeti kapcsolódás és a működés közbeni kapcsolatvesztés külön képernyőt kap. A kliens 2/4/8/16/30 másodperces ütemben újracsatlakozik; a szükséges állapotok visszaérkezésekor automatikusan folytatódik a kijelzés. A token előzetes megújítása normál esetben nem okoz kapcsolatbontást. Az adatlista HTTP(S)-elérési hibájánál a webfelület a konkrét fázist és hibakódot mutatja. [Hibaelhárítás és korlátok](loxone-direct.md).

## Loxone-oldal után eltűnő gesztus vagy betöltőkép

Az F12 javítás az inputhoz köti a gesztust, ezért az újraépített téma képe,
gombja vagy nem buborékoló fedőeleme nem tudja elnyelni. A fix képernyőn
nincs scroll-felismerés. A betöltőkép az adapter saját LVGL-feladatában kerül flushra; a runtime lock nélkül vár a képkocka-visszajelzésre az SD-olvasás/dekódolás előtt; a 85 ms-os
várakozás önmagában nem volt bizonyíték a megjelenésére. Az F13 a fizikai visszaesést okozó 33 ms-os keretet és a kész frame utáni
plusz várakozást visszavonja: a refresh/animáció ismét 15/16 ms-os. Az IRQ-k
között ismételt koordináta nem törli a húzás teljes elmozdulását; az 50 px-es
távolságküszöb megmarad. Az új indulási regresszió minden témán Otthon-nyitás
nélkül is ellenőriz lassú húzásokat. A tényleges paneles javulás és terhelés
újra ellenőrizendő.

## Lassú téma- vagy Otthon-oldalváltás

A fő okok ezek voltak:
- a `fread()` szektoronkénti SD-olvasása,
- a PNG-k kétszeri beolvasása és kitömörítése lapozáskor,
- az Otthon-háttér újraolvasása minden kártyalapnál,
- az azonos értékkel újraírt LVGL-feliratok, -képek és -színek okozta
  folyamatos újrarajzolás (különösen a 4B Slave 33 ms-os frissítésén),
- a zárt diagnosztikai nézethez is 200 ms-onként futó heap- és RSSI-lekérdezés.

Mind javítva van; a mérési módszer és a megmaradt lehetőségek a
[teljesítményleírásban](performance.md#csak-változáskor-rajzoló-frissítés--master-és-4b-slave)
szerepelnek, a fejlesztői szabályok az [AGENTS.md](../AGENTS.md) fájlban. Ha újra
lassulást látsz, először azt nézd meg, hogy egy új kód nem ír-e minden ciklusban
változatlan LVGL-értéket, és nem olvas-e újra változatlan SD-fájlt.

## Csengőhangfeltöltés és SD-foglaltság

A böngészős hangminta a számítógép/telefon hangszóróján szól. Sikeres előnézet
önmagában nem bizonyítja a csengetéskori készülékes lejátszást. A kijelző
betöltési hibája külön jelzi a foglaltságot, hiányzó fájlt, memóriahiányt,
olvasási műveletet/errno-t vagy érvénytelen PCM WAV-ot.

Feltöltéskor a korábbi hangcsomag először törlődik. Fájlonkénti hiba esetén
a többi hang telepítése folytatódik; a weboldalon minden kihagyott fájl neve és
oka látható. Egyetlen telepíthető hang hiánya vagy sérült központi ZIP-könyvtár
esetén a művelet hibával zárul. A már telepített jó fájlok megmaradnak.
Megszakadt hálózati feltöltés után újra fel kell tölteni a csomagot.

Sablon ZIP feltöltésénél az „SD-kártya írási hiba” mostantól az `errno` számát
és szövegét is mutatja, így elkülöníthető a tele kártya, a megnyitott fájlok
korlátja és a kártyahiba. Ha a kijelző éppen hosszú SD-műveletet végez, a
feltöltés megvárja; a dekódolt kép-segédfájlokat ezért külön háttérfeladat
készíti, fájlonként, rövid zárral ([teljesítmény](performance.md)).

A 2026-10-25-i javítás megszüntette a hálózati fogadás alatti hosszú SD-zárat,
különválasztotta a téma- és hangkibontást, és pufferelt stdio hangolvasásra
állt vissza. Az SD-driver/csatolás változatlan. A több változtatásból álló
javítás nem bizonyít külön hardverhibát vagy önmagában hibás DMA-kezelést.
[Részletes javítási és átadási jegyzet](slave-intercom-handover.md).

## „A GitHub nem érhető el, vagy nincs stabil kiadás.”

A gyári témák frissítése fejlesztői (előzetes jelölésű) firmware-en a GitHub
legutóbbi kiadását kérdezi le. Ez az üzenet korábban minden hibát összevont:
nem derült ki, hogy hálózati hiba, HTTP-állapot, váratlan válasz vagy
memóriahiány volt-e. Mostantól a visszajelző sáv és az **Eseménynapló** a
pontos okot mutatja (lásd a [témák dokumentációját](templates.md)), és két
valódi hibalehetőség is megszűnt:

- a válaszpuffer PSRAM hiányában a belső RAM-ból is kérhető (témaváltás után a
  PSRAM-ban kevés a hely),
- a kiadás lekérdezése 1,5 s múlva egyszer újrapróbálkozik, egy sikeres
  feloldás után pedig egy átmeneti API-hiba sem tiltja le a frissítést.

Fejlesztői ZIP-ből telepített téma lehet újabb a legutóbbi kiadásnál; ilyenkor
a lista **Helyi újabb** állapotot ír, és nincs frissíthető téma. Az „Összes
frissítése (0)” gomb mostantól valóban rejtve marad: a `[hidden]` szabály
erősebb a gombok `display` beállításánál.

## Bootciklus: `template_raw` veremtúlcsordulás (2026-10-28)

Soros konzolon a pánik egyértelmű:

```
Guru Meditation Error: Core 0 panic'ed (Stack protection fault).
Detected in task "template_raw" at 0x481ca0ae
Stack pointer: 0x301002f0
Stack bounds: 0x30100318 - 0x30101310
```

A veremmutató a 4 KiB-os tartomány alá csúszott: a feladat verme kevés volt a
`app_templates_raw_step → raw_generate → png_decode/scale_rgba` láncra, ahol
a `fopen/fwrite` és a FAT-réteg is a vermen dolgozik. A lánc három szintjén
összesen ~2,5 KiB-nyi 256–320 bájtos helyi útvonalpuffer volt; ezek a kupacba
kerültek, a verem pedig 8 KiB lett. A feladat minden új mélypontnál naplózza
a szabad vermet (`template_raw szabad verem: … B`).

A hiba akkor jött elő, amikor hiányzó segédfájl miatt a mélyebb `raw_generate`
ág futott; a csak ellenőrző `raw_current` ág belefért a 4 KiB-ba. Az
OTA-visszaállás (rollback) közben rendben működött.

Ugyanennek a feladatnak a verme egy kísérletben PSRAM-ba került
(`xTaskCreateWithCaps`); a pánik ettől függetlenül jelentkezett, tehát nem a
PSRAM-verem volt az ok. A verem azóta belső RAM-ban van.

## SD-hiba nem akadályozhatja az aktiválást és az újraindítást

Hibásra jelölt SD-kártyánál az `app_stats_save_now()` azonnal
`ESP_ERR_INVALID_STATE` hibát ad. A kézi OTA emiatt nem aktiválta a már
ellenőrzött firmware-t („Az SD-statisztika mentése sikertelen; a firmware
aktiválása elmaradt.”), a webes újraindítás pedig elmaradt – csak hardveres
reset maradt. Mindkét útvonal most naplózza a sikertelen mentést
(`APP_EVENT_WARNING`, a hibakóddal), de folytatja a műveletet: hibás kártyára
a napi statisztika úgysem írható ki, a frissítés és az újraindítás viszont
éppen ilyenkor kell.

## Melyik tesztcsomag fut az eszközön?

Minden PR- és kézi CI-build verziója korábban `v0.0.0-ci` volt, ezért a
készülék információs oldalán nem lehetett megkülönböztetni két egymás utáni
tesztcsomagot. A verzió mostantól a commit rövid azonosítóját is tartalmazza:
`v0.0.0-ci.g<commit>`. A szabály változatlan: előzetes jelölésű verzió nem
stabil kiadás, ezért az OTA és a témafrissítés továbbra is a GitHub legutóbbi
kiadását keresi.

## DMA-képes belső RAM: a valódi szűk keresztmetszet (2026-10-28)

A `/api/diagnostics/tasks` soros konzolra írt régiós bontása szerint a belső
RAM nem egyforma:

```
Heap summary for capabilities 0x00000008 (DMA):
  At 0x4ffa2b40 len  32767  free   651  allocated  31360
  At 0x4ffbafc0 len  19440  free    28  allocated  18204
  At 0x4ff92c10 len 164784  free  2192  allocated 161064
  Totals: free 2871 allocated 210628
Heap summary for capabilities 0x00000804 (belső, 8 bites):
  … + At 0x30100198 (TCM, 7784 B) és 0x501081ac (RTC, 32340 B): allocated 0
  Totals: free 41879
```

A TCM és az RTC terület DMA-ra alkalmatlan, ezért a veremcsökkentéssel
felszabadított ~23 KiB oda került, és a DMA-keretet nem növelte. Az SD-vezérlő
és a kriptográfiai gyorsító viszont csak a DMA-képes régiókból tud foglalni,
ahol 210 KiB-ból mindössze ~2,8 KiB maradt szabadon.

Mérések ebből a körből:

- A JPEG-dekóder indulási foglalása mindössze 416 B (113 103 -> 112 687),
  tehát nem ő a fogyasztó; a korai létrehozás maradhat.
- A rendszerindítás végén még 23 875 B szabad DMA-memória van, üzem közben
  2871 B-ra csökken: a különbséget a hálózati kapcsolatok (Loxone WSS,
  böngészőkapcsolatok) pufferei kötik le.

Ezért a webszerver legfeljebb négy nyitott kapcsolatot tart (a legrégebbit
magától bontja), a sablonfájlok olvasása pedig egyetlen, induláskor foglalt
8 KiB-os belső DMA-pufferen keresztül fut, így a betöltés nem függ a
pillanatnyi DMA-helyzettől.

## A 90 fokos tájolás próbaidőszaka megszűnt

A 90 fokos indulás korábban „próba" jelet kapott az NVS-ben, és csak 15
másodperc hibátlan működés után vált véglegessé. Ha az eszköz ezen belül
újraindult (például frissítés vagy kézi reset miatt), a következő indulás
biztonsági 270 fokra váltott, és ismételt eset után le is tiltotta a 90 fokot.
A 90 fokos mód azóta stabil, ezért a próba/letiltás állapot megszűnt: az NVS
csak a kiválasztott tájolást tárolja, a config.json marad a hiteles forrás.

## `stats` veremtúlcsordulás a napi mentésnél (2026-10-28)

```
Guru Meditation Error: Core 0 panic'ed (Stack protection fault).
Detected in task "stats" at 0x481ca428
Stack bounds: 0x4ffaf6f0 - 0x4ffb06e0        (4080 B)
```

A `stats` feladat verme a mért csúcs alapján 6144-ről 4096 bájtra csökkent.
A mérés a másodperces tick 1720 bájtját mutatta, a napi mentés viszont
cJSON-nal és lebegőpontos formázással dolgozik – a veremdump is tele van
double értékekkel –, és ez nem fért bele. A méret visszaállt 6144 bájtra.

Tanulság: a `/api/diagnostics/tasks` csúcsa csak az addig bejárt utakat fedi
le. Ritkán járt, mélyebb ágat (napi mentés, párosítás, hibaoldal) futtató
feladatnál a mért érték nem elég a méretezéshez.

## Névtelen, „Programhiba" okú újraindulás

Ha a soros konzol nincs csatlakoztatva, egy pánik csak a `Reset oka:
Programhiba` mezőben látszik, az okáról semmi. A leggyakoribb ok a
veremtúlcsordulás egy ritkán járt hívási láncon: a belső RAM szűkössége miatt
a vermek mért értékekhez igazulnak, és a mérés csak az addig bejárt utakat
fedi le.

Ezért a firmware percenként megnézi az összes feladat legkisebb
veremmaradékát, és 1024 bájt alatt feladatonként egyszer figyelmeztet az
eseménynaplóban. Ha ilyen sort látsz, az adott feladat vermét kell emelni.
A jelzés a fokozatosan fogyó tartalékot mutatja meg; egy egyszeri, mély ág
(mint a `stats` napi mentése) ettől még figyelmeztetés nélkül csordulhat túl,
ezért a méretezésnél a ritka ágakat is számításba kell venni.

Gyanús újraindulás után érdemes a soros naplót is rögzíteni:
`py -m serial.tools.miniterm COM4 115200 --raw`, majd RESET. A pánik kiírja a
feladat nevét és a veremhatárokat.

## A DMA-keret kimerülése üzem közben (2026-10-28, este)

Tömeges sablonfrissítés (`Összes frissítése`) közben a napló ezt mutatta:

```
E esp-aes: Failed to allocate memory for the array of DMA descriptors
E esp-aes: Generating input DMA descriptors failed
E esp-tls-mbedtls: mbedtls_ctr_drbg_seed returned -0x0001
E sdmmc_cmd: sdmmc_read_sectors: not enough mem, err=0x101
```

Ez a hiányzó láncszem: a **hardveres AES** a TLS-hez DMA-leírókat foglal
DMA-képes belső RAM-ból, és ugyanabból a keretből dolgozik az SD-vezérlő is.
Amikor a keret elfogy, egyszerre áll le a TLS (Loxone WSS, GitHub) és az
SD-olvasás, a kijelző pedig 200 ms-onként újrapróbálja a témabetöltést.
Újraindítás után is azonnal visszaállt, mert a kiindulási keret volt kevés
(10 307 B), nem szivárgás okozta.

Két javítás:

- A hosszú életű, DMA-t nem igénylő feladatok verme és vezérlőblokkja az
  addig üresen álló **RTC-területre** került (`app_task_mem.h`): `stats`,
  `backup_sync`, `presence_nvs`, `time_sync_task`, `wifi_reconnect`. Egyik
  verem sem lett kisebb, a DMA-képes keret mégis ~22 KiB-tal nő. Az RTC
  memória nincs a gyorsítótár mögött, ezért flash-írás közben is biztonságos –
  a PSRAM-mal ellentétben. Ha az RTC-terület betelne, a régi, belső RAM-os
  foglalás a tartalék.
- A FATFS ablak- és munkapufferei nem PSRAM-ból foglalódnak
  (`CONFIG_FATFS_ALLOC_PREFER_EXTRAM` kikapcsolva): PSRAM-ból az SD-vezérlő
  nem tud közvetlenül olvasni, így minden FAT-hozzáférés futásidejű átmeneti
  puffert kért.
