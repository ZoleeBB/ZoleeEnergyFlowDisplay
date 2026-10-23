# Loxone kaputelefon – Master kijelző

A Loxone adatfogadási módban működő Master a kaputelefon közvetlen HTTP/MJPEG
élőképét jeleníti meg. A Loxone 100484 Intercom White helyi videócímét a felhasználó
böngészőben kipróbálta: `/mjpg/video.mjpg`. Más készüléknél kompatibilis MJPEG-cím kell.
A funkció alapértelmezetten ki van kapcsolva, a Slave kijelzőkre nem továbbít videót.
A webes mentés a bemásolt videócím körüli szóközöket eltávolítja; a belső szóköz,
jelszó az URL-ben, HTTPS vagy hiányzó videóútvonal továbbra is hibát jelez.

## Beállítás

A **Loxone beállítások → Kaputelefon (Master kijelző)** részben:

1. Engedélyezd a kaputelefont.
2. Add meg a teljes helyi videócímet, például
   `http://192.168.1.50/mjpg/video.mjpg`. Ez a kaputelefon IP-címe, nem a Miniserveré.
3. Válaszd ki a **Csengetés folyamatban** állapotot a Loxone adatlistából.
   A helyiség és funkció szűrő az Otthon-szerkesztővel közös.
4. Opcionálisan válassz **Kapunyitás** parancscélt és adj meg gombfeliratot.
   Ugyanazok a nem védett Switch / Pushbutton / TimedSwitch objektumok választhatók,
   mint az Otthon pillanatnyi gombjánál. A visszajelző állapot UUID-je nem parancscél.
5. Mentsd el a kaputelefon és Otthon beállításait a közös mentőgombbal.

A videókapcsolat a **külön megadott videós felhasználónevet és jelszót** használja
Az első kérés már Basic Authorization fejlécet küld; a 401-es Digest kihívás is kezelhető. A tesztelt kaputelefonnál csak admin
felhasználóval működik a stream; az integrációs fióknak adott kaputelefon-jogosultság
nem elegendő. Az integráció bejelentkezése változatlan marad. A korábban beállított
kaputelefonhoz egyszer meg kell adni az új mezőket. Üres jelszómező azonos kameracím
és felhasználónév mellett megtartja a mentett jelszót. Új címhez vagy fiókhoz újra kell adni.
A jelszó a `loxone-home.json` fájlban AES-256-GCM titkosítva, eszközkulcshoz,
kameracímhez és felhasználóhoz kötve tárolódik; a webes állapotválasz csak
`has_video_password` jelzőt küld. A videócím és a publikus UI-pillanatkép nem kap
jelszót. A 401-es választ egyszer, azonos címen hitelesítve megismételjük;
a korábbi válasz pufferét az új kérés előtt töröljük. A HTTP videót helyi,
megbízható hálózaton használd. Átirányítást nem követünk.

## Megjelenés és működés

- A kiválasztott numerikus/digitális állapot **1 / true** értéke teljes képernyős,
  fekete hátterű, képarányt megtartó videónézetet nyit, az aktuális oldal fölött.
- Kizárólag ugyanennek az állapotnak a **0 / false** értéke zárja be a nézetet,
  és teszi újra láthatóvá a korábbi oldalt. Nincs bezáró gesztus vagy időzítő.
- Ismeretlen, hibás, más UUID-hez tartozó állapot vagy kapcsolatvesztés nem nulla.
  A nézet ilyenkor megmarad, és szükség esetén hibajelzést mutat.
- Kapunyitó cél nélkül nem jelenik meg gomb. Konfigurált célnál a videó alján
  egy 480×92 képpontos, nagy **Kapunyitás** gomb jelenik meg az egyedi felirattal,
  a külön hangindító gomb mellett.
- A gomb érintésenként egy `pulse` parancsot ad a közös Loxone-kapcsolaton.
  Hosszú nyomás nem ismétel, kapcsolatvesztéskor nincs későbbi automatikus újraküldés.
  A kapunyitás **nem zárja be** a kameraképet. Offline vagy nem támogatott célnál
  a gomb letiltott; a parancsot a worker ismét ellenőrzi elküldés előtt.
- Videóhiba esetén a háttérfeladat újrapróbálkozik. Hibás kamerahitelesítésről,
  memóriahiányról és nem támogatott képről felirat tájékoztat. A kamera HTTP
  állapota, az első teljes MJPEG-képkockára várakozás és az adat nélküli
  kapcsolat külön jelzést kap. A státusz postaládába kerül, így egy elfoglalt
  LVGL-lock vagy a nézet létrehozásával versenyző indítás nem veszítheti el.
  A soros napló HTTP-kódot, fogadott bájt- és képkockaszámot mutat, jelszót nem.

## Erőforrások és korlátok

A videófeladat kizárólag csengetéskor jön létre, és a csengetés végén megszűnik.
A 12 KiB-os stack PSRAM-ban foglalódik, és két hívás között felszabadul;
nem fogyasztja az I2S/Wi-Fi szűk belső DMA-memóriáját. A videó külön,
alacsonyabb prioritású CPU0-feladatban érkezik, nem blokkolja a
Loxone állapotfolyamot. A JPEG-dekódolást az ESP32-P4 hardvere végzi; legfeljebb
10 képkockát jelenítünk meg másodpercenként. Az adatfolyamot folyamatosan ürítjük,
nincs felhalmozódó videósor. Egy tömörített képkocka legfeljebb 2 MiB, a kép
legfeljebb 1280×720, baseline színes vagy szürkeárnyalatos JPEG legyen. A videópufferek csak csengetéskor
foglalódnak: a megjelenített RGB565 kép kb. 1,76 MiB PSRAM, a dekóder eredménye
legfeljebb ugyanennyi, továbbá 2 MiB a tömörített keret. A megjelenített képet
és a dekóder kimenetét külön puffer őrzi, így a rajzolás nem olvas éppen átírt képet.

A konfiguráció a meglévő `loxone-home.json` profil opcionális `intercom` objektuma,
a korábbi profilok alapértelmezetten kikapcsolt kaputelefonnal töltődnek be.
Nincs új SD-erőforráscsomag vagy új kép-/ikonletöltés.

A böngészős kameraelérés igazolt; a firmware valós képfrissítése, a tényleges
kaputelefon JPEG-formátuma és a teljes rendszer együttes terhelése hardveres próbát igényel.

## Fejlesztői ellenőrzés

- `python3 tests/run_tests.py`: teljes helyi regressziós tesztcsomag.
- `python3 tests/test_intercom.py`: tényleges, korlátos MJPEG-keretező és a
  csengetésállapot, konfigurációs roundtrip, hibás címek és kapunyitó parancscél.
- `node tests/test_loxone_home_web.js`: a tényleges szerkesztő kódja, opcionális
  kapunyitás, közös mentés és a hitelesítési adatok kizárása.
- A szimulátor `simulator_intercom` CTest tesztje a tényleges LVGL-felületen
  ellenőrzi a gomb nélküli hívást, a nagy kapunyitót, az egyetlen impulzust,
  a régi kép elutasítását, a hibánál megmaradó nézetet és a felszabadított puffert.
- `tests/test_loxone_protocol.py`: a tényleges közös Loxone-parancssoron azt is
  ellenőrzi, hogy a sorba állítás után véget érő csengetéshez már nem küldünk impulzust.

Az ESP-IDF firmware-build, a bináris-/OTA-méret és a valós kaputelefon együttes
hardveres ellenőrzése szükséges a kiadás előtt.

## Kétirányú beszélgetés

A videó alatt a **Beszélgetés** gomb külön, kézi érintésre indítja a hanghívást.
Kapunyitó konfiguráció esetén a két nagy gomb egymás mellett jelenik meg;
kapunyitó nélkül csak a hanggomb látszik, középen. Nincs automatikus belehallgatás:
a mikrofon csak sikeresen felépült SIP-hívásnál kapcsol be.

- Kapcsolódás közben a gomb **Kapcsolódás megszakítása** felirattal megszakítható.
- Aktív hívásnál a **Beszélgetés vége** gomb leállítja a mikrofont és a hangszórót,
  és bontja a hívást. A videó ettől megmarad.
- A csengetés **0 / false** értéke automatikusan leállítja a hangot és bezárja a videót.
- Loxone-kapcsolatvesztés vagy a kaputelefon beállításainak cseréje leállítja a
  hangkapcsolatot. Sikertelen hívás után nincs automatikus újrahívás.
- Hálózati, SIP- vagy hanghardverhiba szöveges visszajelzést ad. Tíz másodpercnyi
  bejövő RTP-hang hiánya megszakítja a beszélgetést.

A hang célpontját a beállított MJPEG-cím gépnevéből/IP-címéből vesszük; nincs
külön szükséges mező. A hívás a **Loxone Intercom Gen2 közvetlen SIP/TCP 5060**
végpontját (`sip:smarthome@<intercom-ip>:5060`) használja, **G.711 PCMU/PCMA,
8 kHz, 20 ms RTP/UDP** hanggal. A SIP-válaszban kapott médiacímnek ugyanahhoz az
IPv4-címhez kell tartoznia, mint a kaputelefonnak. A kijelző csak a készüléktől
fogad hangot; más állomás nem indíthat mikrofonfelvételt. A kamera a külön
mentett videós fiókkal hitelesít. A használt közvetlen Gen2 SIP-végpont nem kér
külön SIP-fiókot; elutasítás esetén a kijelző hibát mutat, nem próbál más fiókot.

Ez a SIP-elérés nem hivatalos Gen2 interfész; a `markuslaube/loxone-intercom2`
projektben dokumentált közvetlen kapcsolat alapján készült, saját C megvalósítással.
Csak helyi, megbízható hálózatra való: a SIP/RTP útvonal titkosítatlan, nem szabad
5060-as vagy RTP-portot internetre továbbítani. Nem igényel Docker-, NAS-,
FritzBox- vagy SIP-szervert. SIP-hangot nem támogató firmware-nél a videó tovább
működik, de a beszélgetés nem épül fel. A Loxone alkalmazás WebRTC-hívásával való
párhuzamos használat viselkedését a tényleges készüléken kell ellenőrizni.

A kijelző a rögzített Waveshare BSP ES8311 hangkimenetét és ES7210 mikrofonját
használja közös, kezdettől 16 kHz-es, 16 bites Philips sztereó I2S-órán.
Mindkét kodek inicializálása megelőzi a hangfolyam megnyitását. Az első mikrofoncsatorna hangja az Espressif
**ESP-SR 2.6.0 AEC_MODE_VOIP_LOW_COST** visszhangkioltóján halad át; a referencia
a hangszóróra küldött PCM. Nincs letöltött beszédmodell vagy új SD-adatfájl.
Alap hangerő 55%, mikrofonerősítés 24 dB. A visszhangkioltó és a mintapufferek
csak aktív beszélgetés alatt, PSRAM-ban élnek. Az AEC által visszaadott
keretméretet használjuk (nem feltételezünk fix 512 mintát); a csatornapufferek
16 bájtra igazítottak. Az I2S saját DMA-puffereit továbbra is a driver kezeli.
Sikertelen indulásnál a pontos lépés és hibakód látható: például
„Visszhangszűrő memória”, „Hang I2S indítás” vagy „Mikrofon megnyitás”. A BSP I2S-illesztése és kodekeszközei az első
hívás után megmaradnak, de a mikrofon ADC és a hangszóró erősítője zárva van.
A hang külön CPU0-feladatban fut; nem tart LVGL-lockot vagy Loxone-lockot I/O alatt.
Ez a feladat is csak kézi indításkor jön létre, 16 KiB-os PSRAM-stackkel.
Az ESP-IDF `xTaskCreatePinnedToCoreWithCaps` / `vTaskDeleteWithCaps` párja kezeli
a létrehozást és a bontáskori felszabadítást. A videó és a hang együtt 28 KiB-os
stackje hívás közben sem a belső memóriát terheli; két hívás között nem marad
foglalva. A kis TCB-k és az I2S DMA-pufferei továbbra is belső RAM-ban élnek. Ha nincs kaputelefonnézet vagy videópuffer,
a UI a kaputelefon miatt nem kér grafikus lockot és nem invalidál képcache-t.

A valódi hardveres próba szükséges: a felhasználó 100484 készülékének SIP-támogatása,
a pontos kijelzőváltozat mikrofonbekötése, hangerő, visszhang, hangkésés és a
párhuzamos videó/Loxone/Wi-Fi terhelés helyi szimulációval nem igazolható.

### Hangtesztek

`python3 tests/test_intercom_audio.py` a tényleges SIP/SDP/RTP/G.711 kódot
ASan/UBSan alatt ellenőrzi: töredezett TCP-üzenetek, hibás hosszak, idegen
médiacím, egyirányú vagy nem támogatott SDP, kodekhatárok, RTP-időbélyeg
átfordulása, csomagvesztés és sérült fejléc. Helyi TCP/UDP kaputelefon-másolat
mellett a valódi session-kód oda-vissza hangot küld/fogad, ACK/BYE bontást,
CANCEL/200 versenyt, elutasított hívást, hanghardverhibát és ismételt hívásokat
ellenőriz, 256/512/1024 mintás AEC-kerettel; az I2S és az AEC hardver ebben
a tesztben helyettesített. A videóteszt a valódi keretezőn át, darabolt MIME/JPEG
adatokból eljut a képkocka átadásáig; ellenőrzi a 401-es hitelesítési ismétlést,
a válaszpuffer törlését, az elutasított/átirányított és hiányos HTTP-válaszokat is.
A szimulátor a tényleges LVGL kétgombos elrendezést, kézi indítást/leállítást,
hosszú nyomás ismétlésének tiltását és a csengetésvég bontását is ellenőrzi.

Források:

- [Gen2 SIP-híd dokumentáció és forrás](https://github.com/markuslaube/loxone-intercom2/tree/c4269fa49d5eee2bb7f5d80366f27269b7088d3b/sip-directbridge)
- [Espressif ESP-SR visszhangkioltás](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/acoustic_echo_cancellation/README.html)
- [Rögzített Waveshare BSP 2.0.3 forrás](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/0694fdb9fa7751cc2719a9d1927de70547b89ba0/bsp/esp32_p4_wifi6_touch_lcd_x)

### Gyártói mintával összevetett képút

A [Waveshare 10_mp4_player minta](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X/tree/main/examples/esp-idf/10_mp4_player)
konténert bont, hardveresen JPEG-képkockákat dekódol, majd RGB-puffereket ad át a
kijelzőnek. Az MJPEG eleve egymást követő JPEG-képekből áll, itt nincs MP4-konténer.
A közös pont a P4 JPEG-dekóder és az RGB565 képút; a kaputelefon az LVGL-en át adja
át a kész képkockát, hogy a forgatás, gombok és feliratok ugyanabban a rétegben maradjanak.
Nem indítunk második LCD-drivert és nem rajzolunk az LVGL-lel párhuzamosan közvetlenül a panelre.
A duplex hang indítási sorrendjét a [Waveshare LVGL minta](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X/tree/main/examples/esp-idf/08_lvgl_demo_v9)
kodekkezelésével vetettük össze.

### Fordítási hitelesítés és I2S-memória

Az ESP-IDF 5.5.4-ben a HTTP Basic és Digest támogatás alapértelmezetten kikapcsolt.
A firmware mindkettőt külön engedélyezi a `sdkconfig.defaults` fájlban.
Az `app_intercom_build_config.h` leállítja a valódi ESP buildet, ha bármelyik
hitelesítési mód, a külső task-stack vagy a PSRAM XIP hiányzik. A natív teszt
külön fordítással ellenőrzi a helyes és mindegyik hiányos konfigurációt is.

A `Hang I2S indítás (257)` jelentése `ESP_ERR_NO_MEM`: a helyi hangdriver
indítása nem kapott elegendő memóriát. A nagy videó-/hang-stackeket ezért
explicit PSRAM-ba helyezzük, nem csökkentjük a biztonságos stackméreteket.
Az I2S a gyártói BSP duplex hívásánál irányonként legfeljebb három, 160 mintás
DMA-puffert kap. 16 kHz, sztereó, 16 bit mellett ez összesen 3 840 bájt
mintapuffer a korábbi 11 520 helyett. Az alkalmazás linker-wrappere csak
a BSP-port duplex hívását módosítja, a gyártói forrás változatlan. A natív életciklus-
tesztek a tényleges WithCaps létrehozást, PSRAM-képességet, foglalási hibát,
és a hozzá tartozó törlési API-t is számon kérik. A valódi DMA-foglalás sikerét
a fizikai kijelzőn kell ellenőrizni.

### Háttértéma felszabadítása csengetéskor

A kaputelefon megnyitása előtt, még a videófeladat és a nagy képpufferek foglalása
előtt töröljük a háttér LVGL-objektumait, a dekódolt kép-/rasztercache-t, a betöltött
téma JSON-/PNG-pillanatképét és a Loxone-oldalak színes/szürke képpuffereit.
Ez csak egyszer történik egy csengetés elején; üresjáratban nincs új lock vagy
cache-ürítés. Az SD-fájlok, a kiválasztott téma és a kért Otthon-oldal megmaradnak.

Csengetésvégkor az overlay eltűnik, a háttér visszatöltése viszont megvárja a
videó és hang workerének teljes erőforrásbontását. Így a visszatöltődő nagy PNG-k
nem torlódnak össze a kamera/DSP puffereivel. Ezután a meglévő betöltőképernyőn át
újraépül az aktuálisan kiválasztott téma vagy Otthon-oldal, friss adatokkal.
Kapunyitás vagy videóhiba nem indítja el a visszatöltést; a csengetésállapotnak
kell megszűnnie. Ez helyért cserébe egy visszatöltési időt jelent a hívás végén.

A videó indítási hibája elkülöníti a 2 MiB-os PSRAM-bemenet foglalási hibáját
a JPEG-engine hibájától. A dekóder indítási hibakódja mellett a szabad belső
DMA-memória és a legnagyobb összefüggő blokk is megjelenik. A korábbi közös
„nincs elég memória” felirat önmagában nem bizonyította a JPEG-hiba típusát.
A szimulátor valódi Nap pálya téma felszabadítását és változatlan kiválasztással
való visszatöltését ellenőrzi; a natív teszt a tényleges I2S linker-wrappereket
és a teljes médiabontást jelző API-kat is vizsgálja.


### I2S előfoglalás és diagnosztika

Engedélyezett kaputelefonnál a 16 kHz-es duplex I2S adatillesztő már induláskor
lefoglalja a korlátos belső DMA-puffereket, a videó és AEC előtt. Mikrofon-kodek
és hanghívás továbbra is kizárólag a Beszélgetés gomb után nyílik meg.
Újonnan engedélyezett kaputelefonnál az első híváskor, a DSP előtt inicializálunk;
az induláskori előfoglaláshoz egy újraindítás szükséges. Sikertelen I2S-indításnál
a szabad belső DMA-memória és a legnagyobb blokk is megjelenik. A BSP 2.0.3
részleges hiba után törölt csatornahandle-t hagyhat maga után, ezért azt nem
próbáljuk újra használni: a felirat újraindítást kér. A valós hardveres siker
külön kipróbálást igényel.


### HTTP 401 és hang közbeni újraindulás

A Gen2 kamera már az első kérésben megkapja a külön videós fiók Basic
Authorization fejlécét. 401 esetén egyetlen Basic/Digest ismétlés marad;
átirányítás továbbra sem kap hitelesítést. A hibaüzenet jelzi a tényleges kérés
hitelesítési módját, és a külön videós admin adatok ellenőrzésére kér. Jelszó,
Authorization-tartalom vagy kódolt hitelesítés nem kerül a naplóba.

Az IDF 5.5.4 `vTaskDeleteWithCaps(NULL)` belső RAM-os takarítófeladatot hoz
létre, sikertelen foglalásnál abortál. A kész hang-/videóworker most felfüggeszti
magát; a meglévő UI-feladat törli a konkrét handle-t, új memóriafoglalás nélkül.
A rendszer csak a valódi törlés után jelzi, hogy a média teljesen leállt, és
utána indulhat új worker vagy témavisszatöltés. A SIP méretellenőrzése nem másolja
veremre a teljes, legfeljebb 6 KiB-os fejlécet; a parser külön fordítási tesztje
legfeljebb 1 KiB-os függvényenkénti veremkeretet enged.

A hang utolsó műveletének sorszáma RTC RAM-ban marad. Váratlan újraindulás után
a webes főoldal eseménynaplója `Hangkapcsolat közbeni újraindulás` bejegyzésben
mutatja a lépést és a reset okának kódját (feladatindítás, SIP, I2S, AEC, ES8311,
ES7210, olvasás, feldolgozás, lejátszás, bontás vagy felszabadítás). A jelző nem
ír SD-t/NVS-t és nem tartalmaz hitelesítési adatot. A fizikai újraindulás pontos
oka csak a készülék naplójából igazolható; a javítás a bizonyítottan abortáló
önfelszabadítási ágat és a fölösleges veremterhelést szünteti meg.


### AEC-pánik és teljes képkockára várakozás

A készülékről érkezett `AEC feldolgozás (reset=4)` napló a feldolgozóhívás
alatti pánikot jelzi; önmagában nem mondja meg a CPU-kivétel pontos okát.
A hang az FD/FFT útvonal helyett az Espressif VoIP AEC-útvonalát használja,
továbbra is 16 kHz-es PCM-mel és kétirányú SIP/RTP-hívással. Az AEC külső
handle-je mellett a belső feldolgozó létezését is ellenőrizzük; hiányánál
memóriahiba jelenik meg feldolgozóhívás helyett. A pontos készülékes eredmény
újabb próbát igényel.

A JPEG-keret felső határa 2 MiB lett. Teljes kép előtt a képernyő jelzi a
beérkezett KiB-ot, a JPEG-képkezdések és az eldobott túlméretes képek számát.
A gyári P4 dekóder a szürke JPEG-et GRAY kimenetre olvassa; ezt ugyanabban
a képpufferben, hátulról RGB565-re bővítjük. A 8 pixeles szürke MCU-sorlépés és
a 16 pixeles RGB-puffer eltérő kitöltését külön teszt ellenőrzi. Nem kell új
SD-erőforráscsomag. A keretező valós, beágyazott színes/szürke JPEG-ekkel és
256 KiB fölé növelt, érvényes APP-metaadatokkal is tesztelt, külső képletöltés nélkül.
