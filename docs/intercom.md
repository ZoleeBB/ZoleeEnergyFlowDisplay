# Loxone kaputelefon – Master kijelző

A Loxone adatfogadási módban működő Master a kaputelefon közvetlen HTTP/MJPEG
élőképét jeleníti meg. A Loxone 100484 Intercom White helyi videócímét a felhasználó
böngészőben kipróbálta: `/mjpg/video.mjpg`. Más készüléknél kompatibilis MJPEG-cím kell.
A funkció alapértelmezetten ki van kapcsolva, a Slave kijelzőkre nem továbbít videót.

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

Külön kamerafiók nincs: a videókapcsolat automatikusan a **mentett Loxone integráció
felhasználónevét és jelszavát** használja HTTP Basic hitelesítéssel. A fióknak a
kaputelefon közvetlen videóeléréséhez is megfelelő jogosultság kell. A jelszó nem
kerül a videócímbe, az Otthon JSON-ba vagy a webes állapotválaszba; továbbra is a
meglévő titkosított Loxone-tároló kezeli. A HTTP videót helyi, megbízható hálózaton
használd. Átirányítást nem követünk, így más cél nem kaphatja meg a hitelesítést.

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
  memóriahiányról és nem támogatott képről felirat tájékoztat.

## Erőforrások és korlátok

A videó külön, alacsonyabb prioritású CPU0-feladatban érkezik, nem blokkolja a
Loxone állapotfolyamot. A JPEG-dekódolást az ESP32-P4 hardvere végzi; legfeljebb
10 képkockát jelenítünk meg másodpercenként. Az adatfolyamot folyamatosan ürítjük,
nincs felhalmozódó videósor. Egy tömörített képkocka legfeljebb 256 KiB, a kép
legfeljebb 1280×720, baseline színes JPEG legyen. A videópufferek csak csengetéskor
foglalódnak: a megjelenített RGB565 kép kb. 1,76 MiB PSRAM, a dekóder eredménye
legfeljebb ugyanennyi, továbbá 256 KiB a tömörített keret. A megjelenített képet
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
fogad hangot; más állomás nem indíthat mikrofonfelvételt. A kamera továbbra is a
mentett Loxone-fiókkal hitelesít. A használt közvetlen Gen2 SIP-végpont nem kér
külön SIP-fiókot; elutasítás esetén a kijelző hibát mutat, nem próbál más fiókot.

Ez a SIP-elérés nem hivatalos Gen2 interfész; a `markuslaube/loxone-intercom2`
projektben dokumentált közvetlen kapcsolat alapján készült, saját C megvalósítással.
Csak helyi, megbízható hálózatra való: a SIP/RTP útvonal titkosítatlan, nem szabad
5060-as vagy RTP-portot internetre továbbítani. Nem igényel Docker-, NAS-,
FritzBox- vagy SIP-szervert. SIP-hangot nem támogató firmware-nél a videó tovább
működik, de a beszélgetés nem épül fel. A Loxone alkalmazás WebRTC-hívásával való
párhuzamos használat viselkedését a tényleges készüléken kell ellenőrizni.

A kijelző a rögzített Waveshare BSP ES8311 hangkimenetét és ES7210 mikrofonját
használja közös, 16 kHz-es I2S-órán. Az első mikrofoncsatorna hangja az Espressif
**ESP-SR 2.6.0 AEC_MODE_FD_LOW_COST** visszhangkioltóján halad át; a referencia
a hangszóróra küldött PCM. Nincs letöltött beszédmodell vagy új SD-adatfájl.
Alap hangerő 55%, mikrofonerősítés 24 dB. A visszhangkioltó és a mintapufferek
csak aktív beszélgetés alatt élnek. A BSP I2S-illesztése és kodekeszközei az első
hívás után megmaradnak, de a mikrofon ADC és a hangszóró erősítője zárva van.
A hang külön CPU0-feladatban fut; nem tart LVGL-lockot vagy Loxone-lockot I/O alatt.

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
ellenőriz; az I2S és az AEC hardver ebben a tesztben helyettesített.
A szimulátor a tényleges LVGL kétgombos elrendezést, kézi indítást/leállítást,
hosszú nyomás ismétlésének tiltását és a csengetésvég bontását is ellenőrzi.

Források:

- [Gen2 SIP-híd dokumentáció és forrás](https://github.com/markuslaube/loxone-intercom2/tree/c4269fa49d5eee2bb7f5d80366f27269b7088d3b/sip-directbridge)
- [Espressif ESP-SR visszhangkioltás](https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/acoustic_echo_cancellation/README.html)
- [Rögzített Waveshare BSP 2.0.3 forrás](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/0694fdb9fa7751cc2719a9d1927de70547b89ba0/bsp/esp32_p4_wifi6_touch_lcd_x)
