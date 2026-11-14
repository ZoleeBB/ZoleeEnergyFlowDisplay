# Loxone Otthon-vezérlők – ágátfogó code review

[Dokumentációs kezdőlap](README.md) · [Fejlesztés és ellenőrzés](development/README.md) · [Funkciók és képek](features/README.md#otthon-kártyák-leírásai-és-képei)

## Hatókör és eredmény

A 2026. november 14-én indított ellenőrzés a
[`feature/loxone-home-controls` ág / #50 PR](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/pull/50)
teljes fejlesztésére terjedt ki. A review kiinduló tartománya a
`21c6e8a25692b72fe6f8f850707c4bd670191735` alap és a
`6495d117eacf274d3f71224f65022af3f538a038` fejlesztési állapot közötti
166 módosított fájl volt; az itt leírt javítások erre épülnek.

A forrás, az erőforrások, a tesztek, a build és a dokumentáció ellenőrzése
során **három közepes súlyú memória-életciklus hibát és egy kisebb
teljesítményproblémát javítottunk**, továbbá megszüntettünk egy már a főágban
is meglévő, szükségtelen jelszómásolatot. A modellek, a natív Audio és az általános
Home-felület módosításai egymástól független második forrásellenőrzést is
kaptak. Az átnézett területeken nem maradt azonosított, javítatlan blokkoló
hiba. Ez forrás- és regressziós ellenőrzés eredménye; a tartós, fizikai
paneles heap- és veremterhelésre nem jelent korlátlan garanciát.

Az első Audio-kapcsolódás egyszeri timeoutja utáni gyors helyreállást,
valamint a második megnyitás elsőre sikeres működését a felhasználó
elfogadhatónak jelezte. A review ezt a kapcsolódási viselkedést nem
módosította tovább. A korábbi Slave-csengőhang-javítás fizikai újrapróbája
külön nem lett visszaigazolva.

## Megtalált és javított problémák

### P2 – Elhagyott oldalmodellek megtartása nyugtázásra várás közben

Az oldal bezárása vagy váltása korábban csak a generációt érvénytelenítette.
A feldolgozott oldal- és világítás-detail modelleket a Loxone worker a
következő egyeztetési lépésben szabadította fel. Egy már elküldött parancs
legfeljebb 12 másodperces ACK-várása alatt ezért a bezárt nézet adatai
fölöslegesen a memóriában maradhattak.

Az [`app_loxone_lighting_service.inc`](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/blob/feature/loxone-home-controls/main/app_loxone_lighting_service.inc)
most a közös mutex alatt leválasztja és nullázza az érintett mutatókat,
majd a mutex elengedése után felszabadítja a modelleket. A világítás
részletes kezelőjének bezárása csak a részletes modellt szabadítja fel;
a látható oldal modellje megmarad. A generáció és a detail handle miatt
a késői nyugta nem módosíthatja az új nézetet.

A pointerhasználatok ellenőrzése szerint a lockon kívül csak saját
parancsszöveg, skalár azonosító vagy teljes értékmásolat él tovább.
A feldolgozó nem visz kölcsönzött modellmutatót a hálózati várakozásba.
A még nem csatolt új modell tulajdonjogát a reconciliation megtartja;
generációeltéréskor azt is felszabadítja.

**Bizonyítás:** a tényleges service-t fordító teszt 96 ciklust futtat.
A 32 Dimmer- és 32 világítás-detail kör a `reply()` belsejében zárja be
a nézetet, váltakozó sikeres és hiányzó ACK mellett. A további 32 kör
a WindowMonitor dinamikus listájának közvetlen nyitását és bezárását
vizsgálja. A követett allokációk minden körben visszatérnek az elvárt
bázishoz, az ACK-esetekben már a hálózati várakozás befejezése előtt.
A regresszió a javítás előtti
implementációval az azonnali felszabadítást ellenőrző assertnél elbukott.

### P2 – Audio-borítótábla megtartása az utolsó használat után

Az [`app_loxone_audio_media.c`](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/blob/feature/loxone-home-controls/main/app_loxone_audio_media.c) a képek
elengedése után is megtartotta a 13 bejegyzéses médiatáblát. A host
tesztfordításban ez 10 608 bájt volt; ez nem ESP-n mért heapérték.

A tábla most csak az aktuális generáció érvényes, nem üres kép-URL-jének
feldolgozásakor jön létre. A cache törlése elengedi a képreferenciákat,
felszabadítja a táblát, és nullázza a mutatót. A puszta inicializálás,
bezárás, elavult kérés vagy üres borító nem hozza újra létre. Az allokáció
hibája a meglévő visszalépési időzítést használja, ezért nincs minden UI
frissítésnél újabb foglalási kísérlet.

**Bizonyítás:** 64 médiaciklus vizsgálja a sorba tett munka előtti törlést,
a megosztott képet és a UI saját referenciáját, a HTTP-olvasás és a dekódolás
közbeni bezárást, az új generáció indulását régi worker mellett, valamint
az OOM utáni helyreállást. A teszt végén nulla követett média-/workerbájt,
nulla HTTP-kapcsolat és nulla függő callback marad. A korábbi implementáció
az üres médiatáblát és nulla allokációt ellenőrző regresszión elbukott.

### P2 – Home-háttér és logó megtartása sikertelen lapozás után

Lapváltáskor a felület szándékosan újrahasználja az azonos hátteret és
logót. Ha a következő lap ikonjainak betöltése tartósan meghiúsult,
a fallback ág a Home-oldalszámot már nullázta, de a megtartott képeket
nem engedte el. A következő UI-ciklus így kihagyta a normál bezárást.
Egy nagy dekódolt háttér, a logó és kapcsolódó képadatok fölöslegesen
megmaradhattak.

Az [`app_loxone_home_ui.inc`](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/blob/feature/loxone-home-controls/main/app_loxone_home_ui.inc) most a végső
hibakilépés előtt, display-lock alatt meghívja a teljes `home_release()`
takarítást. A régi widgetek ekkor már törölve vannak. Az átmenetileg
foglalt SD-kártya külön várakozási állapot: nem fogyasztja el a tényleges
betöltési hibákra fenntartott 15 próbát.

**Bizonyítás:** a valódi LVGL-előnézet nyolc nappali/éjszakai hibás
lapozási ciklusban ellenőrzi a háttér, a logó, a szürke változatok és
a PNG-erőforrások elengedését, majd a témára visszaállást. Külön 32 SD
várakozási ciklus után sikeres betöltést, illetve várakozás közbeni
kézi bezárást is vizsgál. A célzott ASan-próba RGB565 és XRGB8888 módban
is sikeres; a javítás nélküli kód a megtartott háttér állapotán elbukott.

### P3 – Fölösleges legacy parancskeresés az új kártyák snapshotjában

Az [`app_loxone.c`](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/blob/feature/loxone-home-controls/main/app_loxone.c) az új Controls és Audio kártyákra
is lefuttatta a régi Be/Ki engedélyezési és katalóguskeresési útvonalat,
miközben ezek a típusok saját summary alapján állapítják meg a
vezérelhetőséget. Ez rendszeresen megismétlődő, eredménytelen munka volt
a közös mutex alatt.

A snapshot most csak ezeknél a típusoknál hagyja ki a legacy keresést.
A tényleges `lx_home_command_build()` hívásokat számoló regresszió
Controls- és Audio-oldalnál nulla, régi Pulse kártyánál továbbra is egy
hívást és támogatott eredményt igazol. A teszt a régi kóddal elbukott.
Az ellenőrzés a fölösleges művelet megszűnését bizonyítja; nem állít
mérés nélküli milliszekundumos gyorsulást.

Emellett pontosítottuk a konfigurációmentés kommentjét: mentéskor a
worker lezárja a kapcsolatot és érvényteleníti az Audio-sessiont; csak
a Miniserver újracsatlakozásához használt token marad a helyi változóban.
Ez dokumentált meglévő működés, nem új futásidejű változtatás.

### P3 – Örökölt, szükségtelen kamera-jelszómásolat a UI-pillanatképben

Az `app_loxone_home_snapshot()` a teljes belső profil másolásával a
`video_password` mezőt is átvitte a tartós GUI-pillanatképbe, pedig
a Home-felület ezt nem használja. Ez a kiinduló főágban is jelen volt,
tehát nem ennek a fejlesztési ágnak a regressziója. A review nem talált
ehhez kapcsolódó külső adatkiadást; a probléma a fölösleges belső másolat.

A snapshot most a másolás után explicit `mbedtls_platform_zeroize()`
hívással törli a cél teljes jelszómezőjét. A kamerakapcsolat által használt
belső profil változatlan marad. A regresszió nem üres tesztjelszóból
indul, a snapshot teljes mezőjének nullaságát és az eredeti adat
megőrzését külön ellenőrzi.

## Memória és háttérmunka az Otthon-oldalak bezárása után

| Erőforrás | Bezárás utáni állapot | Miért így működik? |
| --- | --- | --- |
| Oldalwidgetek, popupok, háttér, logó, ikonok | A UI bezárási út elengedi; a hibás lapozás fallbackje is takarít | Rejtett oldal nem igényli ezeket a képeket és widgeteket |
| Feldolgozott világítás-, IRC-, árnyékolás- és Controls-modellek | Lock alatt leválasztva, utána felszabadítva | Nem várnak egy elküldött kérés nyugtázására |
| WindowMonitor-lista, világítás-detail és körlisták | Az érintett oldal/detail bezárásakor felszabadulnak | Csak a látható kezelőhöz tartoznak |
| Audio-képreferenciák és 13 elemű médiatábla | A UI lekapcsolja a képforrást, a cache elengedi a referenciákat és a táblát | A még aktív worker vagy UI saját referenciája biztonságosan érvényben marad a saját használata végéig |
| Audio WebSocket, RX-sor, részleges üzenet és ideiglenes metadata | A tulajdonos következő biztonságos vezérlési lépésében leáll és felszabadul | Callback vagy hálózati művelet alatt nem szabad idegen szálból felszabadítani |
| Folyamatban lévő HTTP-képmunka és dekóder | Érvénytelen generációt felismer, az aktuális műveletből kilépve takarít | A bezárás nem vár hálózatra UI/model lock alatt; használt puffer nincs idő előtt elengedve |
| Sorban váró régi oldalparancs | A következő ellenőrzés generációeltérés miatt eldobja | Bezárt laphoz nem indul új parancs; a már elküldött kérés nyugtája még megérkezhet |
| Közös Miniserver-kapcsolat és nyers állapotcache | Megmarad | A Miniserver a teljes kezdőállapot után változásokat küld; a közös kapcsolat más funkciókat is kiszolgál |
| Audio sessiontükör az érvényes Miniserver-kapcsolathoz | Megmarad, visszavonáskor a JWT teljes puffere nullázódik | Újranyitáskor szükséges; nem új teljes auth-handoff történik minden oldallépésnél |
| Konfiguráció, katalógus és statikus `TEMPLATE_RAM` pillanatképek | Megmaradnak a közös PSRAM munkaterületen | Ezek közös vagy előre lefoglalt állapotok; nem elfelejtett dinamikus oldalfoglalások |
| Kamera-jelszó a Home UI-pillanatképében | A snapshot elkészítésekor a teljes mező nullázódik | A GUI nem használja; a belső profil hitelesítője a kamerakapcsolathoz szükséges |

A nyers cache korlátja 512 egyedi állapot-UUID, összesen 64 KiB szöveg,
egy szöveges állapot legfeljebb 32 KiB. Rejtett oldalhoz nem tartunk
feldolgozott vezérlőmodellt. A `home_config`, `home_next_config`,
`home_values` és `home_next_values` statikus PSRAM-pillanatképek maradnak;
az oldal bezárása ezért nem jelenti minden Home-hoz köthető bájt
megszűnését.

Az Audio-runtime 64 ismételt ciklusa a greeting, kulcskérés, auth és READY
fázisokban is vizsgálja a bezárást, 24 sorban váró RX-üzenettel,
részleges fragmenttel és auth előtti metadata-val. A kapcsolat leállítása
közben meghívott DISCONNECTED callback is része a próbának. A sorok
létrehozása/törlése kiegyenlített, bezárás után nincs új küldés,
a kapcsolat egyszer áll le és egyszer szabadul fel. Csak a szükséges
sessiontükör marad meg; visszavonáskor annak teljes JWT-puffere is törlődik.

Az átmenetileg offline, de továbbra is látható oldal más életciklus:
az ilyen nézet kép- és widgetreferenciái a tényleges bezárásig
szándékosan megmaradhatnak.

## Más funkciókra gyakorolt hatás ellenőrzése

| Terület | Ellenőrzött szerződés és regressziós határ |
| --- | --- |
| Új vezérlők | Dimmer, CentralLightController, Slider, TimedSwitch, WindowMonitor, Gate, Window: tartományok, tiltások, típushoz kötött parancsok, késői ACK, lapváltás |
| Natív Audio | Auth/session, JWT-törlés, callback-generáció, RX-fragmentek, kedvencek ismétlésének tiltása, képreferenciák, URL-korlátok, dekóderhatárok, OOM és visszalépés |
| Korábbi Home-típusok | Világítás, IRC, redőny, régi Pulse és általános kártyák; lock, popup-azonosítás, közös modellek, reconnect |
| Konfiguráció és webszerkesztő | Nyolc lap és 96 kártya, séma/migráció, típusszűrés, fülek, fókusz, rendezés, részmentések és nem mentett adatok megőrzése |
| Üzemmódok | A natív kártyák csak a 10.1-es Master közvetlen Loxone módjában aktívak; MQTT, HTTP, adatforrásként Slave és a 4B kizárási kapui megmaradnak |
| Energia, presence és közös állapot | A közös Miniserver-kapcsolat és fogyasztóértékek normál Home-lapváltáskor nem szűnnek meg |
| OTA és kaputelefon | A meglévő OTA worker osztott, egyfeladatos végrehajtója; firmware-prioritás; callback lockon kívül; kamera előtt az Audio képmunka biztonságos kifutása |
| Slave csengőhang | Master átmeneti 503 megkülönböztetése; korlátos újrapróbálás; profil/revízió/csengetés elavulásának ellenőrzése az utolsó olvasás után is; rövid olvasás és EINTR takarítása |
| Home v4 SD-csomag | Verzióellenőrzés, erőforráskorlátok, igazított PSRAM/POSIX beolvasás, hibás csomag takarítása, dekódolás előtti méretellenőrzés |
| Build és generált fájlok | A kiadott webfejléc és generátor egyezése; változatlan LVGL 9.5.0 / adapter 0.6.4 szerződés; a feloldott függőséggráf valódi firmware-fordítása |

A review-javítások nem hoznak létre új állandó taskot, és nem növelik
a taskvermeket. Az ESP32-P4 Master v3.0–v3.99 és a 4B Slave v1.0–v1.99
buildhatárokat a firmware CI ellenőrzi. A `.su` kapu az egyes függvények
veremkeretét méri; ez nem azonos a teljes hívási lánc valós csúcsterhelésével.

## Dokumentációs lefedettség

A [funkcióindex táblája](features/README.md#otthon-kártyák-leírásai-és-képei)
mind a nyolc új blokkot közvetlen leírás- és képhivatkozással sorolja fel.
A [Controls-útmutató](loxone-home-controls.md), az
[Audio-útmutató](audio-controller.md) és az
[Otthon-konfiguráció](v3-home-config.md) a használati korlátokat,
beállításokat, parancsokat, működési és memóriahatárokat is tartalmazza.
A funkció-, integráció-, téma- és fejlesztési indexek a meglévő
[dokumentációs struktúrához](documentation-structure.md) igazodnak.

A képek valódi LVGL-előnézetből származnak, megjelölt tesztadatokkal.
A hat korábbi Controls- és három Audio-kép a friss ASan-build renderével
képpontról képpontra egyezik; a Slider részletes kezelőjéhez külön új kép
készült. A galériákban szerepel a képek eredete és előállítása.
Az index a korábbi világítás-, IRC- és redőnyfunkciók képeit is elérhetővé
teszi; a gyökér README arculati és támogatási hivatkozásai megmaradtak.

## Ellenőrzési bizonyítékok és lezárási kapu

| Ellenőrzés | Eredmény a review során |
| --- | --- |
| Audio modell, auth, runtime, média és képfeldolgozás | Sikeres célzott ASan/UBSan-próbák, valódi mbedTLS RSA2048/RSA4096 és JPEG-dekódolás; 64 runtime- és 64 médiaciklus |
| Controls modell és service | Sikeres ASan/UBSan, 96 bezárási ciklus, O0/O2 veremkeret-kapuk |
| Valódi közös Loxone-protokoll | Sikeres RSA/AES/HMAC, parancssor, session-handoff és snapshot-hívásszámláló regresszió |
| Világítás, IRC és árnyékolás | Sikeres célzott modell/service és veremkeret-próbák |
| Home modell, tároló és webszerkesztő | Sikeres séma, OOM, tároló/HTTP hibainjektálás, jelszó- és üzemmódhatárok, kiadott JavaScript |
| Valódi LVGL Home-hibakilépés | Sikeres RGB565/XRGB8888 ASan-próba, háttér/logó elengedés, SD-várás és helyreállás |
| Teljes helyi RGB565 LVGL-regresszió | Sikeres ASan-futás, 100 életciklus és 400 kirajzolt oldal; utána az új hibakilépés, energia-, presence- és Slave-próbák is sikeresek |
| Dokumentáció és képek | Minden új blokk leírása és képe elérhető; friss renderrel ellenőrzött PNG-k; 673 helyi link, 75 elérhető dokumentációs oldal, 64 külön ellenőrzött fejezethivatkozás |
| Teljes host regresszió | `python3 tests/run_tests.py`: sikeres, exit 0; a kiadási/USB-csomag és annak offline dokumentációja is ellenőrzött |
| Végleges Master és 4B firmware, teljes CI | A publikált review-commitokon kötelező kapu; eredménye és firmware-artifactja a PR-ben szerepel |

A helyi ASan-futtatásoknál a környezet ptrace-korlátja miatt
`detect_leaks=0` szerepel; a célzott memória-próbák ettől független,
explicit allokáció- és erőforrásszámlálást használnak. A CI tesztszabályait
nem lazítottuk.

A PR akkor kész a lezárás előtti végső jóváhagyásra, ha a teljes host
regresszió és a végleges publikált állapot mindkét firmware-fordítása
zöld, a generált állományok és a dokumentációs kapuk rendben vannak,
és nincs közben megváltozott ágfej vagy feloldatlan review-megállapítás.
A PR összefoglalója rögzíti a végleges commitokat, CI-futást és artifactot.
