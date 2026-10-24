# Loxone kaputelefon – Master kijelző

## Ideiglenes helyi hangszóróteszt

A webes kaputelefon-beállításoknál a **Hangszóróteszt indítása** gomb az ott
beírt hangerővel folyamatos 1000 Hz-es szinuszhangot indít. Ehhez nem kell
menteni a hangerőt; a beszélgetéshez a mentett érték érvényes. Újabb kattintás
leállítja a tesztet. Aktív hívással nem futhat együtt; csengetéskor vagy a
kaputelefon konfigurációjának cseréjekor automatikusan leáll. A mikrofont,
SIP/RTP-t és AEC-t nem használja. Ugyanaz az ES8311, I2S és hangszóró-erősítő
szólal meg, mint a bejövő beszédnél. A webes állapot külön jelzi a foglaltságot
és a hardveres indítás/lejátszás hibáját. A végpont hitelesített, korlátos és
ugyanazt a kérésazonosítót igényli, mint a beállításmentés.

A folyamatos tesztciklus nem készít teljes Loxone-pillanatképet és nem vár
a Loxone mutexére a 20 ms-os hangblokkok között. Rövid, nem várakozó ellenőrzés
figyeli a csengetést és a konfigurációgenerációt; pillanatnyi mutexfoglaltságnál
a következő blokkban próbálja újra. A helyi teszt és a hívás prioritása is 7,
így a GUI számolása nem szoríthatja ki a rövid hang-írást.
Az I2S DMA-keret változatlan; a készülékes folyamatosságot továbbra is meg kell vizsgálni.

Ha a teszthang hallható, a helyi lejátszási út működik. Ha nem, önmagában még
nem dönthető el, hogy a hangszóró, a bekötés, a kodek/erősítő vezérlése vagy az
I2S a hibás. Külső mágnes hatását eltávolított és visszahelyezett mágnessel,
azonos teszthang és hangerő mellett lehet összehasonlítani.

A Loxone adatfogadási módban működő Master a kaputelefon közvetlen HTTP/MJPEG
élőképét jeleníti meg. A Loxone 100484 Intercom White helyi videócímét a felhasználó
böngészőben kipróbálta: `/mjpg/video.mjpg`. Más készüléknél kompatibilis MJPEG-cím kell.
A funkció alapértelmezetten ki van kapcsolva, a Slave kijelzőkre nem továbbít videót.
A webes mentés a bemásolt videócím körüli szóközöket eltávolítja; a belső szóköz,
jelszó az URL-ben, HTTPS vagy hiányzó videóútvonal továbbra is hibát jelez.

## Beállítás

A folyamatos helyi teszthang és az akadozó beszélgetés különbsége a hálózati és
DSP-útvonal vizsgálatát indokolja. A beszélgetés most előbb írja a bejövő blokkot
a hangszóróra, utána olvas mikrofont és futtat AEC-t. A kodekek 16 kHz-en
maradnak; a mikrofon első csatornájának két-két szomszédos mintája átlagolva
kerül a 8 kHz-es VoIP AEC-be. A 8 kHz-es kimenet közvetlenül tölti a G.711
20 ms-os csomagjait, változatlan 8 kHz-es RTP-időalappal. A hívás közbeni
állapotellenőrzés sem másol teljes konfigurációt vagy vár Loxone-lockra.
A felirat az AEC feldolgozási idejét is mutatja; ez a készülékes próbán
segít megkülönböztetni a DSP-terhelést az I2S-/hálózati várakozástól.

Az Espressif a VoIP módhoz 8 és 16 kHz-et dokumentál:
[AEC módok és erőforrásigény](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/acoustic_echo_cancellation/README.html).
A helyi tesztek a minták és időbélyegek helyességét igazolják; a fizikai
beszélgetés folyamatossága, hangerő, torzítás és visszhang készülékes ellenőrzést igényel.

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
- A **0 / false** csengetésjel akkor zárja be a nézetet, ha nincs felépült
  hanghívás. Az aktív beszélgetés ugyanazzal a hívásazonosítóval nyitva tartja
  a videót. A hang bontásakor a korábbi oldal visszatér, ha már nincs csengetés.
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

A videófeladat csengetéskor jön létre, és a csengetés vagy az aktív beszélgetés
végéig él. A nézet bezárása után felszabadul.
A 12 KiB-os stack PSRAM-ban foglalódik, és két hívás között felszabadul;
nem fogyasztja az I2S/Wi-Fi szűk belső DMA-memóriáját. A videó külön,
alacsonyabb prioritású CPU0-feladatban érkezik, nem blokkolja a
Loxone állapotfolyamot. A JPEG-dekódolást az ESP32-P4 hardvere végzi; legfeljebb
10 képkockát jelenítünk meg másodpercenként. Az adatfolyamot folyamatosan ürítjük,
nincs felhalmozódó videósor. Egy tömörített képkocka legfeljebb 2 MiB, a kép
legfeljebb 1280×720, baseline színes vagy szürkeárnyalatos JPEG legyen. A videópufferek csak csengetéskor
vagy aktív beszélgetéskor élnek: a megjelenített RGB565 kép és a legfrissebb
képet fogadó tartalék puffer egyenként kb. 1,76 MiB PSRAM, a dekóder eredménye
legfeljebb ugyanennyi, továbbá 2 MiB a tömörített keret. A videófeladat a
tartalék pufferbe másol **LVGL-lock nélkül**. A UI a saját zárolása alatt
cseréli fel a két képpuffert, így a rajzolás nem olvas éppen átírt képet.
Nincs képsor: mindig a legfrissebb teljes kép marad meg. Az első kép a nézet
létrehozása előtt is fogadható. A régi generáció elutasított, bontáskor az
elfogadás azonnal megszűnik, a pufferek a producer leállása után szabadulnak fel.

A korábbi átadás 100 ms-os LVGL-lockot kért a videófeladatból; sikertelen lock
ugyanazt a „kamera kész, kijelző még nem vette át” feliratot adta, mint a
hiányzó nézet/puffer vagy eltérő generáció. A felhasználói felirat ezért nem
bizonyította az elutasítás pontos okát. A postaládás átadás ezt a lockfüggést
és az induláskori nézetlétrehozási versenyt megszünteti. A célzott valódi UI-teszt
szándékosan tiltott 100 ms-os lockkal, korai képpel, újabb képre cserével,
kitöltött sorokkal, gyors újracsengéssel és késleltetett felszabadítással fut.

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
  és bontja a hívást. A videó aktív csengetés esetén megmarad.
- Felépült beszélgetést a **0 / false** csengetésjel nem bont. Még csak kapcsolódó
  hívást a csengetés megszűnése továbbra is megszakít.
- Sikeresen elküldött **Kapunyitás** impulzus után **5 másodperccel** a hang
  automatikusan bontódik. Ismételt kapunyitás nem halasztja el a határidőt;
  sikertelen küldés nem indít időzítőt. Ha közben megszűnt a csengetés, a nézet
  is bezáródik; aktív csengetés esetén a videó megmarad.
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
**ESP-SR 2.6.0 AEC_MODE_VOIP_LOW_COST** visszhangkioltóján halad át 8 kHz-en; a referencia
a hangszóróra küldött PCM. Nincs letöltött beszédmodell vagy új SD-adatfájl.
A hangszóró hangereje a webes **Kaputelefon** beállításokban menthető, egész
0–100%-os érték (0 = némítás). Régi profilnál az alapérték 55%. A kijelzőn nincs
hangerőcsúszka. A következő hívás a mentett értéket használja. Ugyanitt a
**Mikrofon erősítése** külön 0–100%-os egész érték: 0 némítás, 100 a kodek
37,5 dB-es maximális erősítése. Régi profilnál és új beállításnál 80% (30 dB),
a korábbi fix 24 dB helyett. A százalék a kodek erősítési tartományát skálázza,
nem a hangnyomás százaléka; a hardver a támogatott lépésekre kerekíthet.
A visszhangkioltó és a mintapufferek
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
ellenőriz, 128/256/512 mintás, 8 kHz-es AEC-kerettel, kétszer ennyi 16 kHz-es I2S-mintával; az I2S és az AEC hardver ebben
a tesztben helyettesített. A videóteszt a valódi keretezőn át, darabolt MIME/JPEG
adatokból eljut a képkocka átadásáig; ellenőrzi a 401-es hitelesítési ismétlést,
a válaszpuffer törlését, az elutasított/átirányított és hiányos HTTP-válaszokat is.
A szimulátor a tényleges LVGL kétgombos elrendezést, kézi indítást/leállítást,
hosszú nyomás ismétlésének tiltását, az aktív hang melletti csengetésvéget és
a kézi/kapunyitás utáni bontás feltételeit is ellenőrzi.

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
videó és hang workerének képpuffer-/DSP-erőforrásbontását. Így a visszatöltődő nagy PNG-k
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
8 kHz-es DSP-PCM-mel és kétirányú SIP/RTP-hívással, miközben az I2S 16 kHz marad. Az AEC külső
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

### JPEG-motor korai foglalása és a hang fogadási kompatibilitása

A készülékes hibajelzés `JPEG-dekóder indítás (257)` mellett 4075 B szabad
belső DMA-memóriát és 1024 B legnagyobb blokkot mutatott. Ez nem a PSRAM
képpufferének hibája. Loxone módban a JPEG-motor most a konfiguráció betöltése
után, a Wi-Fi/BLE előtt létrejön, és két hívás között megmarad. Nem indít kamerát,
mikrofont vagy új feladatot. A nagy tömörített/dekódolt képpufferek továbbra is
csak csengetéskor foglalódnak, és utána felszabadulnak. Más adatfogadási mód
nem foglal JPEG-motort. A korai foglalás hatása firmware-újraindítással érvényesül.
Hiba esetén a boot tovább fut, és a videófeladat újrapróbálja a motor létrehozását.
A soros napló az előkészítés eredményét és a DMA-memória előtte/utána értékeit mutatja.

Az [Espressif #18914 hibajegy](https://github.com/espressif/esp-idf/issues/18914)
az SDK 6.0.2-ben megerősített, fenntartott DMA-poolt kizáró JPEG-foglalást ír le.
A nálunk rögzített [5.5.4 dekóder](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_jpeg/jpeg_decode.c)
és `jpeg_private.h` ugyanezt a `MALLOC_CAP_DEFAULT`-ot is kérő descriptorfoglalást
használja. A hibajegy a saját eszközünk pontos allokációs hibapontját nem bizonyítja;
a korai foglalás a késői töredezettséget és a későbbi erőforrás-versenyt kerüli meg,
külső SDK-forrás módosítása nélkül.

Az [RFC 3264, 5.1/6.1](https://www.rfc-editor.org/rfc/rfc3264) szerint az SDP
médiaportja fogadóport, nem kötelező küldőport. A hang most a SIP-ben ellenőrzött
kaputelefon-IP első érvényes RTP-csomagjának küldőportját rögzíti; ettől kezdve más
portot vagy SSRC-t továbbra sem fogad. A kimenő hang változatlanul az SDP-ben
megadott célra megy. Ha mindkét G.711 kodek szerepel a válaszban, a fogadás PCMU
és PCMA csomagokat is dekódolhat, a küldés a válasz első támogatott kodekjét használja.
Hibás csomag nem rögzíthet forrásportot. A helyi TCP/UDP-teszt eltérő küldőporttal,
PCMU-küldéssel és PCMA-visszaúttal is végigjárja a tényleges worker hangútját.

A hang felirata másodpercenként mutatja az RX, jó és TX csomagszámot és a helyi
fogadóportot. A tíz másodperces időtúllépés külön megadja az idegen forrás,
nem tárgyalt kodek és hibás/késői RTP miatt elutasított csomagok számát is.
A gyártói/nyilvános referenciák és a natív hardverhelyettesítő tesztek nem
helyettesítik a fizikai kaputelefonnal és kijelzővel végzett közös próbát.

### Helyreállás a hang lejátszási órájának elcsúszása után

A készülékes visszajelzés szerint a videó már működik. A hangnál 549 fogadott
csomagból 45 volt elfogadott, 504 RTP-hibás; a forrás- és kodekelutasítás nulla.
Ez igazolja az adatérkezést, de a korábbi összesített RTP-számláló nem különíti el
az SSRC-, fejléc- és időbélyegproblémát. A kódban ettől függetlenül reprodukálható
hiba volt: a 256 ms-os jitterablakból kieső adatfolyam tartósan kieshetett,
mert a lejátszási mutató soha nem igazodott az új csomagokhoz.

A fogadó azonos IP/port/SSRC mellett két új, egymást követő sorszámú és
összefüggő időbélyegű csomaggal állítja helyre az elcsúszott lejátszási pozíciót.
Az első ablakon kívüli csomag még nem állítja át a puffert. A megerősítés
az [RFC 3550 A.1](https://www.rfc-editor.org/rfc/rfc3550#appendix-A.1)
kétcsomagos visszaállási elvéből indul ki; ez alkalmazásoldali jitterkezelés,
nem a teljes RFC-s referenciaimplementáció átvétele. Régi/duplikált sorszám,
hibás csomag vagy eltérő SSRC nem szinkronizálhat. Az elavult mintajelölők
törlődnek; nincs új heapfoglalás és a puffer mérete változatlan. A részben
elkésett csomag még fel nem használt végét a fogadó megtartja.

A diagnosztika külön számolja a fejléc (`H`), SSRC (`S`), teljesen késő (`L`)
és ablak elé kerülő (`A`) csomagokat, valamint az újraszinkronizálásokat (`R`).
`F` a forrás-, `K` a kodekelutasítás; az L/A számláló a helyreállításban részt
vevő csomagokat is számolja. Aktív hívásnál a legnagyobb feldolgozási ciklusidő
és az AEC-keret hangideje is látszik (`ciklus: mért/elvárt ms`), hogy a tényleges
I2S/AEC-terhelés elkülöníthető legyen a hálózati hibától.

A natív teszt 30 másodpercnyi hangot vezet át eltérő, ±20%-os lejátszási
óraütemmel, és megköveteli a hang folytatását. Vizsgálja a részben késő csomagot,
a kétcsomagos helyreállást, a sorszám-/időbélyeg-átfordulást és a régi/idegen/
hibás csomagok elutasítását. A készüléken a javulást és a kétirányú hang minőségét
újabb próbával kell ellenőrizni; a videó működő foglalási útvonala nem változik.

### Készülékes visszajelzés és hangütemezés (2026-10-23)

A videó működését a készülékes próba igazolta. Hangnál RX:2483, jó:2419,
TX:2173, újraszinkron:29 és 50/16 ms ciklus mellett továbbra sem volt hallható
beszélgetés. Az elfogadott RTP-csomag önmagában nem jelent hallható beszédet.
A ciklusidő első száma csúcsérték, nem átlag; most másodpercenként újraindul.
A blokkoló I2S-olvasás mellett a keretenkénti további RTOS-várakozás megszűnt.
A jelszintfelirat váltakozva mutatja a két másodperces PCM-csúcsot (0–100%):
bejövő G.711 után, mikrofon bal/jobb, AEC után. Ezzel a hálózati csend,
a néma/eltérő mikrofoncsatorna és a feldolgozás során eltűnő jel elkülöníthető.
A hangszóró fizikai megszólalását és a kétirányú beszédet továbbra is készüléken
kell igazolni; a jelszint nem helyettesíti ezt a próbát.
