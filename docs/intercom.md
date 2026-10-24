# Loxone kaputelefon – Master kijelző

A Loxone adatfogadási módban működő Master csengetéskor teljes képernyős
HTTP/MJPEG-kameraképet nyit, kézzel indítható kétirányú beszélgetéssel és
opcionális kapunyitással. A funkció alapértelmezetten kikapcsolt; a Slave-ek
nem kapnak videót vagy hangot. Nem kell külön SIP-szerver, NAS vagy új SD-csomag.

Az [Energiaáramlás tiltás](energy-flow-disable.md) mellett is használható.
Üres Otthon esetén a Master 10%-os logós készenlétből a napszakhoz beállított
fényerővel nyitja meg, és bezáráskor a logóhoz, illetve 10%-ra tér vissza.
A Kaputelefon saját mentésgombja csak a kaputelefon-szakaszt módosítja.

## Beállítás

A **Loxone beállítások → Kaputelefon (Master kijelző)** részben:

1. Engedélyezd a kaputelefont.
2. Add meg a kaputelefon saját helyi videócímét, például
   `http://192.168.1.50/mjpg/video.mjpg`. Ez nem a Miniserver címe.
3. Add meg a külön videós felhasználónevet és jelszót. A tesztelt Loxone
   100484 Intercom White kameráján admin fiók kellett; az integrációs
   felhasználó kaputelefon-jogosultsága nem volt elegendő.
4. Válaszd ki a **Csengetés folyamatban** állapotot a Loxone adatlistából.
   A helyiség és funkció szűrők az Otthon-szerkesztővel közösek.
5. Opcionálisan válassz **Kapunyitás** parancscélt és adj meg gombfeliratot.
   Az Otthon pillanatnyi gombjánál támogatott, nem védett Switch / Pushbutton /
   TimedSwitch célok használhatók. Az állapot UUID-je nem parancscél.
6. Állítsd be a **Hangszóró hangereje** és **Mikrofon erősítése** értékét.
   Mindkettő 0–100 közötti egész szám; 0 némítást jelent.
7. Ments a közös **Kaputelefon és Otthon-beállítások mentése** gombbal.

A hangszóró alapértéke 55%, a mikrofoné 80%. A mikrofon százaléka a kodek
0–37,5 dB-es erősítési tartományát skálázza: 80% = 30 dB, 100% = 37,5 dB;
a hardver a támogatott lépésekre kerekíthet. Ez nem hangnyomásszázalék.
A mentett értékeket a következő hívás használja. A kijelzőn nincs hangerőcsúszka.

A videócím körüli szóközök mentéskor eltűnnek. HTTPS, URL-be írt jelszó,
belső szóköz vagy hiányzó videóútvonal nem támogatott. Azonos kameracím és
felhasználónév mellett az üres jelszómező megtartja a mentett jelszót; új
címhez vagy fiókhoz újra meg kell adni.

## Megjelenés és működés

- A kiválasztott csengetésállapot **1 / true** értéke képarányt megtartó
  videónézetet nyit az aktuális oldal fölött.
- A **0 / false** érték akkor zárja be, ha nincs aktív beszélgetés.
  A felépült hangkapcsolat a csengetés megszűnése után is nyitva tartja a videót.
  Ismeretlen/hibás állapot vagy kapcsolatvesztés nem számít nulla jelnek.
- A **Beszélgetés** gomb indítja a hangot. Nincs automatikus belehallgatás:
  a mikrofon csak sikeres SIP-hívás után kapcsol be.
- Kapcsolódás közben **Kapcsolódás megszakítása**, aktív hívásnál
  **Beszélgetés vége** a gomb felirata. Kézi bontáskor a videó megmarad,
  ha még van csengetés. A csak kapcsolódó hívást a csengetés vége megszakítja.
- Kapunyitó cél nélkül csak a hanggomb látszik. A **Kapunyitás** érintésenként
  egy impulzust küld; hosszú nyomás nem ismétel, offline kérés nem kerül későbbi
  újraküldésre. A worker elküldés előtt újra ellenőrzi a kérés érvényességét.
- A helyben elfogadott **Kapunyitás** kérés után **5 másodperccel** az aktív
  hangkapcsolat automatikusan bontódik. Nem vár a Miniserver nyugtájára;
  ismételt nyomás nem tolja ki a határidőt. Elutasított helyi kérés nem indít
  időzítőt. Ha már nincs csengetés, a videóablak is bezárul; különben megmarad.
- Kapcsolatvesztés vagy konfigurációcsere leállítja a hangot. Híváshiba után
  nincs automatikus újrahívás. A kamera hibánál újrapróbálkozik.

### Feliratok és hibajelzés

A kijelző kapcsolódáskor röviden jelzi a kamera HTTP-indítását, az első teljes
képre várakozást, illetve a hang kapcsolódását, a kaputelefon válaszára várást
és a hanghardver előkészítését. Az első megjelenített kép után a videó
kapcsolódási felirata eltűnik; létrejött hangkapcsolatnál a hang státuszsora üres.
A hívás bontógombja továbbra is jelzi, hogy a beszélgetés aktív.

Normál beszélgetés közben nincs RX/TX-, jelszint-, AEC- vagy ciklusidő-kijelzés,
és nem futnak az ezekhez hozzáadott idő-/PCM-csúcsmérések. A kamera sem mutat
bájt- vagy képkezdésszámlálót. A kapunyitási kérés rövid folyamatjelzése megmarad.

Hiba esetén továbbra is megjelenik a hitelesítési, hálózati, nem támogatott
képi vagy hanghardverhiba. Tíz másodpercnyi elfogadott RTP-hang nélküli
kapcsolat bontódik, érthető üzenettel. A részletes RTP-hibaszámlálók ilyenkor
a soros naplóba kerülnek. A napló és a váratlan hang-újraindulás webes
eseménybejegyzése segíti a hibakeresést; jelszót vagy hangmintát nem tárolnak.

## Helyi hangszóróteszt

A webes **Hangszóróteszt indítása** gomb az ott megadott hangerővel folyamatos
1000 Hz-es szinuszt játszik; újabb kattintás leállítja. Ehhez nem kell menteni
a hangerőt. A beszélgetés továbbra is a mentett értéket használja.

A teszt ugyanazt az I2S/ES8311/hangszóró-erősítő útvonalat használja, de nem
nyit mikrofont, SIP/RTP-kapcsolatot vagy AEC-t. Aktív hívással nem futhat együtt;
csengetéskor vagy konfigurációcserekor automatikusan leáll. A webes állapot jelzi
a foglaltságot és a hibát. A végpont hitelesített és korlátos.
A teszt kézzel indítható diagnosztikai lehetőségként megmarad, nem indul el magától.

## Hálózat és hitelesítés

A kamera külön videós fiókja nem változtatja meg a Miniserver integrációs
bejelentkezését. Az első HTTP-kérés Basic hitelesítést küld; 401 esetén egyetlen
Basic/Digest ismétlés lehetséges, a korábbi válaszpuffer törlésével.
Átirányítást nem követünk. A jelszó a `loxone-home.json` profilban AES-256-GCM
titkosítva, eszközkulcshoz, kameracímhez és felhasználóhoz kötve tárolódik;
a webes állapot csak `has_video_password` jelzőt ad vissza.

A hang célja a videócím gépneve/IP-címe, a közvetlen Gen2
`sip:smarthome@<intercom-ip>:5060` végpont. SIP/TCP 5060, G.711 PCMU/PCMA,
8 kHz-es időalap és 20 ms-os RTP/UDP csomagok használatosak. A kaputelefon
ellenőrzött IPv4-címének első érvényes RTP-küldőportját rögzítjük; más IP,
port vagy SSRC nem vehető át. A kimenő hang az SDP fogadóportjára megy.

Ez nem hivatalos Gen2 SIP-interfész. A helyi HTTP/SIP/RTP útvonal
titkosítatlan, megbízható helyi hálózatra készült; nem kell internetes
porttovábbítás. SIP-hangot nem támogató készülék/firmware esetén a videó
külön működhet. Más modellek, firmware-ek és a Loxone alkalmazás WebRTC-hívásával
párhuzamos használat külön készülékes próbát igényelnek.

## Erőforrások és médiaút

A videó JPEG-képenként legfeljebb 2 MiB tömörített adatot kezel, hardveres P4
dekódolással, legfeljebb 1280×720 mérettel. Színes és szürke JPEG is RGB565
képpé alakul. A JPEG-motor Loxone módban a Wi-Fi/BLE előtt készül elő,
és két hívás között megmarad. A nagy képpufferek csak nyitott nézethez élnek.

A dekóder LVGL-lock nélkül másol a legfrissebb teljes képet tároló PSRAM-pufferbe.
A UI a saját zárolásánál cseréli a két képpuffert; nincs képsor, és a megjelenített
puffert a producer nem írja át. Az első kép már a nézet létrehozása előtt
fogadható. A tartalék képpuffer legfeljebb 1,76 MiB további PSRAM-ot igényel.

Az ES8311 hangkimenet és ES7210 mikrofon 16 kHz-es, sztereó, 16 bites I2S-en fut.
A bejövő 8 kHz-es hangot előbb továbbítjuk a DAC felé, utána olvasunk mikrofont
és futtatunk 8 kHz-es VoIP AEC-t. Az első mikrofoncsatorna két szomszédos
időmintájának átlaga adja az AEC-bemenetet; a referencia az előző lejátszott blokk.
Az AEC-kimenet közvetlenül tölti a 160 mintás G.711 csomagokat.

Az RTP-puffer kapacitása 256 ms; induláskor és újraszinkronizáláskor
60 ms-nyi tartalékot képez, a DSP-blokkmérettől függetlenül. Részben késett
csomag használható végét megtartja. Elcsúszott lejátszási pozíciót csak két új,
sorban érkező és összefüggő időbélyegű csomag állíthat helyre. Az elveszett
hangminták helyén csend marad.

A hangfeladat prioritása 7; állapotellenőrzése nem vár a Loxone mutexére,
és az I2S órája ütemezi. A DSP-pufferek PSRAM-ban élnek. Engedélyezett
kaputelefonnál a kis duplex I2S DMA-puffer korán foglalódik: összesen 3840 bájt.
Újonnan engedélyezett funkciónál az előfoglalás újraindítás után érvényesül.
A videó 12 KiB-os és a hang 16 KiB-os task-stackje PSRAM-ban él; a worker
felfüggesztése után a meglévő UI-feladat végzi a WithCaps törlést.

A háttértéma erőforrásai a nagy médiapufferek előtt szabadulnak fel.
A nézet bezárása után a korábbi téma/Otthon-oldal a teljes médiabontás
befejeztével töltődik vissza, friss adatokkal. A fájlok és a kiválasztás megmaradnak.
A konfiguráció a meglévő `loxone-home.json` opcionális `intercom` objektuma;
régi profilnál a funkció kikapcsolt, a hangerők alapértékei automatikusak.

## Ellenőrzés és kiadás

A felhasználó 2026-10-24-én készüléken visszaigazolta a működő videót, folyamatos
helyi teszthangot, megfelelő kétirányú beszédet és a kapunyitás utáni automatikus
bontást. Ezt követi a kijelző diagnosztikai feliratainak eltávolítása; a médiaút,
a hangerők és a hangbontás szabályai változatlanok.

- `python3 tests/run_tests.py`: teljes C/JS/protokoll/kiadási regresszió.
- `python3 tests/test_intercom_audio.py`: valódi SIP/SDP/RTP/G.711 és
  TCP/UDP session-kód, ASan/UBSan, 128/256/512 mintás AEC-keretek,
  csatorna-/időbélyeghelyesség, kézi és időzített kodekzárás/SIP BYE.
  A hardver és az Espressif DSP helyettesített.
- `python3 tests/test_intercom.py`: tényleges MJPEG-keretező, színes/szürke
  JPEG-fixture, nagy és darabolt képek, hibás címek, csengetésmodell.
- Loxone-protokoll-, HTTP-, tároló- és JS-tesztek: mentés, titkosított jelszó,
  hangerőhatárok/alapértékek, kapunyitási sor és nyugtától független időzítés.
- Chromium desktop/mobil: tényleges beágyazott kaputelefon-szerkesztő és hangteszt.
- LVGL/ASan 32 bit és RGB565: korai és legfrissebb kép, másolási tulajdonjog,
  generációk, gyors újracsengés és késleltetett felszabadítás.
- A `simulator_intercom` CTest a valódi LVGL-elrendezést, kézi hívásindítást,
  csengetés utáni nyitva tartást és az aktív hívás üres státuszsorát is ellenőrzi.
- ESP-IDF 5.5.4 CI: Master/Slave firmware, partíció-/OTA-méret és USB-csomag.

A saját **#20 PR mainre összevonása v3.1.0 kiadást** készít, a szokásos
patch-növelés helyett. Összevonás nélküli lezárás nem ad ki verziót.
A pontos szabály és ütközésvédelem: [Automatikus kiadás](releases.md).
A záró módosítás után az aktuális PR checks eredménye az irányadó;
más hardveren/kaputelefonon a működés külön ellenőrzendő.

## Források

- [Gen2 SIP-híd dokumentáció és forrás](https://github.com/markuslaube/loxone-intercom2/tree/c4269fa49d5eee2bb7f5d80366f27269b7088d3b/sip-directbridge)
- [Espressif ESP-SR visszhangkioltás](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/acoustic_echo_cancellation/README.html)
- [RFC 3550 – RTP](https://www.rfc-editor.org/rfc/rfc3550)
- [Rögzített Waveshare BSP](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/0694fdb9fa7751cc2719a9d1927de70547b89ba0/bsp/esp32_p4_wifi6_touch_lcd_x)

A párosított 4B kijelzők és a csengőhangcsomag működése: [Slave kaputelefon](slave-intercom.md).
