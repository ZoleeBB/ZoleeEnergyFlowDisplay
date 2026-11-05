# Waveshare 4B – 720×720-as Slave firmware

Az [Energiaáramlás tiltás](energy-flow-disable.md) külön szabályai: Loxone Otthon-alapnézet, megőrzött témák és Slave logós képernyő.

A fő Rendszer téma Energiaáramlás kártyája a Waveshare
ESP32-P4-WIFI6-Touch-LCD-4B (SKU 31416) eszközön, falon vagy asztali tartón,
SD nélkül, kizárólag Slave szerepben.

A [célprogram leírása](../firmware/slave-waveshare-4b/README.md) tartalmazza a
hardveradatokat, buildet, telepítést, szimulátort, közös modulokat, memóriakezelést
és hardveres ellenőrzőpontokat.

A [rétegleírás](../firmware/slave-waveshare-4b/design/README.md) rögzíti az
elfogadott tájat, az eltérő ablakfényes házakat és a natív energiakijelzést.
A képekre nem kerül mérési adat vagy háttérmaszk.

A fő kijelző külön cél marad. A közös BLE, PIN/profil, időszinkron, Wi-Fi és OTA
modulok biztosítják a protokollazonosságot. Az új panel saját 4B BSP-t és UI-t kap.
A szimulátor ugyanezt a UI-t fordítja.

## Betűméretek és olvashatóság

A fejlécen kívül minden felirat egy mérettel nagyobb (16 → 20, 20 → 24,
28 → 34 képpont, legalább +20%): a csomópontnevek, a teljesítményértékek, az
akkumulátor részletsora, az állapotsor, a diagnosztika, a „Mai összesítő" lapjai,
a PIN-pad és a kaputelefon szövegsorai. A két új méret (24, 34) a meglévő
`tools/prepare_fonts.py`-vel, ugyanabból a Noto Sans forrásból készült; a Slave
a 16 képpontos készletet már nem fordítja be. A fejléc (cím, időjárás,
páratartalom, szél, óra) mérete szándékosan változatlan.

Az elrendezés a nagyobb sorokhoz igazodik: a kör melletti név+érték pár a kör
magasságának közepén marad, a kör alatti pár pedig az akkumulátor
részletsorával együtt is egymásba lógás nélkül fér el.

A várakozó állapotüzenetek három ponttal végződnek („Master keresése..."), mint
a fő kijelzőn: a tipográfiai „…" nincs a beágyazott betűkészletben, ezért
korábban üres négyzetként jelent meg.

## Párosítás: mikor vész el, mikor nem

A Slave a hitelesítési hibát korábban mindig úgy kezelte, hogy **eldobta a
tárolt párosítási kulcsot**. Egyetlen időtúllépés – például ha a Master épp
újraindul vagy frissül a hitelesítés közben – ezzel véglegesen elrontotta a
kapcsolatot: a Slave utána megtalálta a Mastert, egy pillanatra ki is írta a
címét és az RSSI-t, majd a sikertelen titkosítás után újra keresni kezdett, és
ebből magától nem jött ki.

Mostantól a kulcsot csak akkor dobja el, ha a Master **ténylegesen kulcshiányt
jelez** (HCI 0x06). Ilyenkor a kijelzőn megjelenik, hogy mit kell tenni: a
Master **Beállítások** oldalán az „Új párosítás engedélyezése 2 percre"
gombbal nyílik az ablak, a PIN pedig a Master diagnosztikáján olvasható, és a
Slave PIN-padján kell beírni. Sikeres hitelesítés után az üzenet eltűnik.

## Beragadt csatlakozás

A Slave a megtalált Masterhez azonnal kapcsolódni kezd. Ha a CONNECT esemény
elmarad (például megakad a hosted/C6 link), korábban örökre „csatlakozás alatt"
maradt: a kijelzőn egy pillanatra megjelent a Master címe és az RSSI, utána
semmi, és a keresés sem indult újra. Mostantól 15 másodperc után a próbálkozás
megszakad, a napló rögzíti („csatlakozás időtúllépés"), és a keresés folytatódik.

## Önjavítás: a kijelzőkör megállása

A fényerő és az érintés is a 33 ms-os LVGL-körből jön: ha az megáll (beragadt
zár vagy driver), a kijelző sötét marad, és érintésre sem ébred – korábban ezt
csak tápelvétel oldotta fel. A főciklus mostantól figyeli a kör időbélyegét, és
30 másodperc állás után újraindítja az eszközt (OTA-művelet alatt nem). Az
újraindítás oka NVS-be kerül, és a következő induláskor a napló első sorai
között olvasható a diagnosztikán.

## Önjavítás: BLE-figyelő

A kijelző a Mastertől kapja az összes adatot, ezért ha a kapcsolat tartósan
hiányzik, az eszköz magától újraindul:

- ha a BLE host **90 másodpercen belül nem szinkronizál** (a C6 társprocesszor
  nem válaszol), vagy
- ha **10 percig nincs masterkapcsolat** a szinkronizált host mellett.

A további próbálkozások között 30, majd 120 perc szünet van (a számláló NVS-ben
él, és az első sikeres masterkapcsolatnál nullázódik), így egy tartósan
kikapcsolt Master mellett sem indul újra percenként. PIN-bevitel és
kaputelefon-hívás alatt a figyelő vár.

Az újraindítás oka minden esetben NVS-be kerül, és a következő induláskor a
napló első sorai között megjelenik a diagnosztikán.

## Fényerő SD-kártya nélkül

A 4B-nek nincs SD-kártyája és saját beállítóoldala, ezért a **nappali és
éjszakai fényerőt a Mastertől kapja**: a védett profil (1004 karakterisztika) a
Wi-Fi és az automatikus frissítés mellett a Master `display.day_brightness` és
`display.night_brightness` értékét is átviszi. A blob hossza és verziója
szándékosan változatlan (102 bájt, 1-es verzió), a két érték a jelszó mögötti
kihasználatlan bájtokban utazik: így egy régebbi firmware-ű Slave is beolvassa
a profilt, csak a fényerőt hagyja figyelmen kívül. 62 bájtnál hosszabb Wi-Fi
jelszó mellett nincs hely a fényerőnek, ilyenkor a Slave a sajátját tartja. A
Master Beállítások oldalán átírt fényerő **újraindítás nélkül**, másodperceken
belül érvényesül a Slave-en is; a profil többi mezőjének változása továbbra is
mentés utáni újraindítást kér.

A 0 nem érvényes fényerő (a tartományon kívüli masterérték a 80/20
alapértékre esik vissza) – az éjszakai teljes sötétítést a jelenlétszabály
intézi, nem a fényerő.

Régebbi, a fényerőt még nem küldő Masterrel a Slave a korábban mentett
fényerejét tartja meg.

## Háttérvilágítás: a panel valódi tartománya

A 4B háttérvilágítását boost meghajtó kapcsolja (GPIO26 PWM, GPIO33 BL_EN), és
**48% alatt egyszerűen kialszik**: 48%-on még világít, 47%-on már sötét. A
küszöb éles és ismételhető, tehát a panel sajátossága, nem szoftverhiba – a BSP
a kért értéket hibátlanul beállítja, a soros napló is azt mutatja
(`Setting LCD backlight: 30%`).

Ezért a Slave a kijelzőlogika 1–100%-os skáláját a hardver használható
tartományára képezi le:

| Kért | Panel |
| --- | --- |
| 0% | 0% (teljes sötét – az éjszakai sötétítéshez ez kell) |
| 1% | 48% |
| 20% | 57% |
| 30% | 63% |
| 100% | 100% |

Közte lineárisan. A Mastertől kapott 20–30%-os éjszakai fényerő így látható
képet ad, a sötétítés pedig változatlanul igazi 0%-ot. A diagnosztika és az
eseménynapló mindkét értéket mutatja: `Fényerő: 30% (panel 63%)`.

A PWM a BSP 5 kHz-es alapértékén marad: a 48%-os küszöböt ezen mértük, és ilyen
éles küszöbnél a lassabb PWM csak vibrálást hozna. A Master 10.1-es panelje más
meghajtót használ, ott a teljes 1–100% működik, ezért ez a leképezés csak a
4B firmware-ében van.

## Éjszakai sötétítés és jelenlét

**A sötétítésről a Master dönt**, mert csak nála van a Loxone jelenlétállapota;
a Slave a 20 bájtos jelenlétcsomag 0. bitjét hajtja végre. A három eset:

- **„Sötétítés éjszaka, ha nincs jelenlét" kikapcsolva** → a kijelző egész éjjel
  az éjszakai fényerőn marad, a jelenlétjel nem számít.
- **Bekapcsolva, beállított jelenlétforrással** → folyamatos 1 mellett soha nem
  sötétül el; a visszaszámlálás csak a jelenlét megszűnésekor (érvényes 0 érték)
  indul, és a profil időkorlátja után sötétít. Ha a Master nem kap érvényes
  jelenlétértéket (nincs Loxone-kapcsolat, vagy a kiválasztott állapot nem küld
  értéket), **nem kér sötétítést**: a kijelző világos marad.
- **Bekapcsolva, jelenlétjel nélkül** („Csak érintéses ébresztés") → éjszakai
  módban azonnal elsötétül, és csak érintésre ébred.

A Loxone csak változáskor küld értéket, ezért a jelenlét órákig ugyanaz az 1
vagy 0 lehet; a Master a megkapott állapotot tartja, a frissességet a
15 másodperces keepalive adja. A jelenlétvesztés ideje a WS-kapcsolat
újraépülését is túléli, különben az újra megkapott, változatlan 0 újraindítaná
a visszaszámlálást, és a kijelző kigyulladna az éjszaka közepén.

BLE-kapcsolatvesztéskor a Master utolsó döntése marad érvényben. A Slave helyi
jelenléttartása 15 másodperc – csak egy-két kimaradt olvasást hidal át, nem ez
dönt a sötétítésről.

Az érintés mindig ébreszt: az első, sötétből indított érintést a felület
elnyeli (nem kattint), de a fényerőt visszakapcsolja, legalább 15 másodpercre –
akkor is, ha a profil időkorlátja ennél rövidebb. Nulla fényerőről
visszatéréskor a BSP háttérvilágítását külön is felkapcsoljuk.

Nyitott diagnosztika alatt nincs sötétítés: a hibakeresést nem szakíthatja meg
az időkorlát lejárta.

## Kijelző tájolása és hálózati cím

A 4B kijelző elforgatása a Master `/loxone` oldalán, a Slave-kijelzők kártyáján
állítható (0°, 90°, 180°, 270°). A beállítás a meglévő, hitelesített
jelenlétprofillal utazik BLE-n; a Slave elmenti, és **rövid késleltetés után
újraindul**, mert futó MIPI DSI mellett nincs átforgatás. Indulás után a kép és
az érintés is a kért tájolásban működik. Kaputelefon-hívás vagy PIN-bevitel alatt
a Slave megvárja a végét.

A 4B-nek nincs saját weboldala, ezért a jobb felső sarok megérintésére teljes
képernyős **diagnosztika** nyílik, „Bezárás" gombbal. Amit kiír:

- firmware-verzió, üzemidő, tájolás,
- Master azonosító, BLE állapot és RSSI, a BLE host szinkronja és a keresés
  állapota (a BLE a C6 társprocesszoron át fut),
- Wi-Fi állapot, IP-cím, időszinkron,
- napszak és a ténylegesen beállított fényerő,
- éjszakai sötétítés és időkorlát, a jelenlét állapota (aktív/nincs, friss-e a
  minta és hány másodperce), a sötétítés visszaszámlálása,
- indulás óta érzékelt érintések száma,
- a **rendszernapló** utolsó bejegyzései (csak memóriában, görgethető): itt
  látszik, mikor sötétült el vagy ébredt fel a kijelző, mikor érkezett érintés,
  mikor változott a jelenlét, és mi történt a legutóbbi frissítéssel.

Indulás után a napló első sorai megmutatják, ha az **előző OTA visszaállt**: a
nem véglegesült verziót és – ha az alkalmazás még le tudta írni – a visszaállás
okát is (melyik feltétel hiányzott az ötperces ellenőrzésből). Ez az egy
adat túléli az újraindítást, a napló többi része nem.

A napló és az eseménymásolat PSRAM-ban készül, és csak nyitott diagnosztikánál
frissül, ezért a 33 ms-os megjelenítő kört nem terheli.

## Frissítés

A Slave a saját csomagját tölti le (`ZoleeEnergyFlowDisplay-slave-4b-<tag>-ota.bin`),
nem a Masterét. A napváltáskor (és az első szinkron után) induló automatikus
ellenőrzést eddig is magától futtatta, ha a Mastertől kapott profilban be van
kapcsolva az automatikus frissítés, és van Wi-Fi-kapcsolata.

A Master webes **„frissítések keresése"** gombja mostantól a párosított
Slave-eknek is szól: a kérés a meglévő, hitelesített jelenlétcsatornán megy át,
és a Slave egyetlen ellenőrzést indít. Wi-Fi nélkül vagy futó művelet mellett az
ellenőrzés kimarad, és ez a Slave rendszernaplójában is megjelenik.

## Szilíciumrevízió – a 4B panelek v1.x lapkával is érkeznek

A megvásárolt 4B panel ESP32-P4 **v1.3** szilíciummal érkezett, ezért a Slave
firmware a `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` + `CONFIG_ESP32P4_REV_MIN_100`
párossal készül: a kép v1.0 – v1.99 lapkán indul. A PSRAM órajele marad 200 MHz
(ezt a v1.x is támogatja, csak a 250 MHz-es mód v3.x-only), így a 720×720-as
felület sebessége nem változik.

A két revíziócsalád az ESP-IDF-ben kizárja egymást, tehát ez a bináris v3.x
lapkán nem indul el. Ha később v3.x-es 4B panel érkezik, ahhoz külön build kell
(`SELECTS_REV_LESS_V3` nélkül, `REV_MIN_300`-mal) – a fő kijelző (10.1) ma is
így, v3.0 – v3.99 tartományra készül.

A kiadás csomagolása kiolvassa a bootloader és az alkalmazás fejlécéből a
revíziókövetelményt, és eltérésnél hibával leáll, így rossz minimummal nem
kerülhet ki csomag. Ha mégis revízióhibát kapsz telepítéskor, a
[USB telepítés](usb-install.md) hibakeresési szakasza írja le, mit jelent.

## v3 ébresztési bekötés – fejlesztési állapot

A 4B és a fő kijelző Slave módja már a közös fényerő-állapotgépet használja.
Csak friss Master-minta változtathat nappalt/éjszakát; megszakadt kapcsolat vagy
lejárt minta az utolsó ismert napszakot megőrzi. Induláskor, PIN-bevitelkor és
használható érintőkapu hiányában a kijelző látható marad a saját fényerőértékein.

A nyers LVGL-érintőolvasás elé kötött közös kapu az első, sötétből ébresztő
érintés lenyomását, mozgását és felengedését is elnyeli. A következő érintés
már kattinthat. A BSP olvasója, koordinátái és adatmutatói megmaradnak.
A valódi LVGL-teszt a 720×720 és az 1280×800 szimulátorban is fut.

Az eszközönkénti stabil identitás, a Master NVS-profilja, a védett `1006` BLE-
olvasás, a Loxone WS-jelenlétút és a két panel közös profiladaptere be van kötve.
A Slave másodpercenként olvassa a 20 bájtos opcionális profilt. Rövid impulzus
számlálóval, tartós aktív jel szinttel jut át; késői/replay/lejárt válasz nem ébreszt.
SMP titkosítás/MITM/bond/16 bájtos kulcs kötelező.

Az új profilban a sötétítés **alapból kikapcsolt**. A Master Loxone webes
beállítójában a már párosított kijelző neve, jelenlétforrása, timeoutja és az
éjszakai sötétítés kapcsolója szerkeszthető. A felület külön jelzi az átvett,
függő, hibás és tartósan mentett konfigurációt; a helyi szerkesztés frissítéskor
megmarad. Régi Masterrel továbbra is a saját folyamatos nappali/éjszakai fényerő érvényes. A fogadott profil disconnect
alatt memóriában marad; saját fényerőinek korábbi NVS-beállítása nem változik.
Részletes szerződés: [Slave jelenlétprofil](slave-presence-protocol.md).
Fizikai paneles próba még szükséges.

## Mai összesítő

Vízszintes húzással a 4B két nézet között vált: energiaáramlás és **Mai
összesítő**. Az összesítő a mastertől a védett `1005` karakterisztikán kapott
napi adatokat mutatja (termelés, fogyasztás, vételezés, visszatáplálás, akku
töltés/leadás, önellátás, saját felhasználás, költség, bevétel, megtakarítás).
Első szinkronig `—`, kapcsolatvesztéskor az utolsó ismert mai kép látszik
elavultságjelzéssel. A napi statisztika-pillanatképet a firmware PSRAM-ba, csak
látható nézetnél és legfeljebb másodpercenként kéri le; üresjáratban a 33 ms-os
kör nem érvénytelenít. Kezelés: [gesztusok](gestures.md#waveshare-4b-slave-720720).

## Kaputelefon

A Masteren kezelt kamera, hang, kapunyitás és csengőhang a 4B-n is elérhető; a hívásfoglalás központi. [Részletes működés és hardveres átvétel](slave-intercom.md).
