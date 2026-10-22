# Bluetooth adat seed – master kijelző

A webes **Beállítások → Bluetooth adatmegosztás → Adat seed – master kijelző (BLE)**
kapcsolóval a fő kijelző energiaadatokat tesz elérhetővé a közelben lévő slave-eknek.
A kapcsoló alapértéke kikapcsolt; régi SD-konfigurációval sem indul el automatikusan.
A `config.json` gyökérmezője: `"DataSeedEnabled": 0` vagy `1`.
Mentés után a szokásos szabályozott újraindítás alkalmazza a változást.

## Kapcsolódás

- BLE GAP név: **ZoleeEnergy** (aktív keresés scan response mezőjében).
- Saját GATT szolgáltatás: `8b7e1000-6c8a-4f57-9b2e-5d1a7c0e9f30`.
- Energia karakterisztika: `8b7e1001-6c8a-4f57-9b2e-5d1a7c0e9f30`.
- Időjárás karakterisztika: `8b7e1002-6c8a-4f57-9b2e-5d1a7c0e9f30`.
- Óra karakterisztika: `8b7e1003-6c8a-4f57-9b2e-5d1a7c0e9f30`.
- Mindhárom karakterisztika READ + NOTIFY, nincs alkalmazásadat-írás.
- A CCCD-re a szokásos `01 00` írás kapcsolja be az adott értesítéseket.
- A három mérési karakterisztika nyilvánosan olvasható; jelszó nincs bennük.
- A **1004** karakterisztika külön, PIN-nel hitelesített és titkosított Wi-Fi/OTA profilt ad.
  LE Secure Connections, MITM, bonding és 16 bájtos kulcs szükséges hozzá;
  Just Works, legacy pairing és rövid kulcs nem fogadható el.
- A host legfeljebb három kapcsolatot kezel; a tényleges határt a C6 vezérlő firmware-e is korlátozhatja.
- Kapcsolódás után szabad kapacitás esetén folytatódik a hirdetés; bontás után újraindul.

A slave szolgáltatás-UUID alapján keressen, ne kizárólag név alapján.
Több master azonos nevet hirdethet: a kiválasztott master BLE címét érdemes megjegyezni.
A jelenlegi P4 firmware is használható slave-ként az alábbi adatfogadási móddal.
A kisebb S3 kijelzők külön firmware-e későbbi fejlesztés.

## Slave adatfogadási mód

A Beállításokban válaszd a **Slave (Bluetooth)** adatfogadási módot. Minimális
SD `config.json`: `{"DataReceptMode":"slave"}`. Nincs szükség SSID-re, brokerre,
HTTP- vagy Loxone-beállításokra, NTP-kiszolgálóra vagy kézi időzónára. Ezeket a
felület Slave módban elrejti, illetve a Wi-Fi/masterfüggő működését megjelöli.
SD-kártyával a helyi sablonok és statisztikai tárhely használható; SD nélkül
a kijelző az alábbi korlátozott, beépített Rendszer módban működik.

SD-vel a Slave saját magának integrálja és menti a fogadott élő mintákat,
és minden telepített témát használhat. A master korábbi történetét nem másolja
át. Az eltérő kezdési idő és adatrések miatt az összesítők eltérhetnek.

## SD nélküli Slave – csak Rendszer téma

Új, korábbi fő kijelzőként nem azonosított eszköz SD nélkül automatikusan Slave
módban indul. Bekapcsolás után keresd meg a master diagnosztikáján a PIN-t,
nyisd meg a kétperces párosítási ablakot, majd írd be a slave érintős PIN-padén.
Nem kell külön firmware vagy partícióváltás. Ez a jelenlegi P4 hardverre készül;
más méretű/hardverű kijelzőhöz továbbra is külön port szükséges.

- Csak a firmware-be épített **Rendszer / System** nézet érhető el. Nincs
  SD-s képfájl, ZIP, témaletöltés, vízszintes témaváltás vagy helyi történeti archívum.
  A diagnosztika és a PIN-pad továbbra is használható.
- A webes beállítások belső NVS-be mentődnek, verziófüggetlen JSON-ként,
  legfeljebb 3072 bájtban. Slave módból SD nélkül nem lehet más adatfogadásra
  vagy masterre váltani; a backend ezt manipulált webkérésnél is ellenőrzi.
- A párosítás, Wi-Fi/OTA profil és tájolás is NVS-ben marad. A Wi-Fi és
  automatikus frissítés továbbra is a mastertől öröklődik.
- Az SD állapota **nem** lesz mesterségesen sikeres: a diagnosztika külön
  jelzi az SD nélküli működést és a master-statisztika frissességét.

Biztonsági korlát: a firmware SD-s induláskor tartósan megjegyzi, hogy az eszköz
fő kijelző vagy Slave volt. Ismert fő kijelző SD-hibája **nem** vált automatikusan
Slave-re. Régebbi firmware-ről frissítve először a meglévő SD-vel induljon el az
eszköz, hogy szerepét eltárolja; régi tájolási/master-PIN nyom esetén a jelölő
nélküli eszközt is konzervatívan fő kijelzőnek tekintjük. Korábban főként használt
eszköznél az SD-s konfigurációban válassz Slave-et, indítsd el egyszer SD-vel,
majd kikapcsolás után vedd ki a kártyát. Érvénytelen belső konfigurációnál nincs
csendes alaphelyzetbe állítás. A behelyezett SD-t a következő újraindítás veszi át.

### A diagramok és napi összesítők szinkronja

Nem teljes SD-megosztást használunk. Az új, **1005** GATT karakterisztika a
Rendszer nézethez szükséges, 6664 bájtos pillanatképet szolgálja ki: a mai 288
ötperces pont öt energiaáramlási komponensét és hiányjelölőit, napi és lezárt
összesítőket, tegnapi összehasonlítást, valamint a master pénznemét és energia/
CO₂/fa-számítási alapértékeit. Nincs helyi újraintegrálás: a master a hiteles forrás.
Az élő teljesítmény/SOC/időjárás/óra a meglévő három értesítési csatornán marad.

A 1005 READ + WRITE jogosultsága **LE Secure Connections, MITM, bonding,
16 bájtos kulcs és titkosítás**. Az írás csak olvasási kurzort mozgat, nem
módosít fájlt vagy beállítást. A 8 bájtos kérés verzió/op/lap/transzferazonosító
mezőkből áll; 14 lap követi, laponként 12 bájtos fejléc és maximum 480 bájt adat.
Az ATT hosszú olvasás kisebb MTU-val is működik, a slave 247-es MTU-t kér.
Mindhárom kapcsolat külön, legfeljebb 45 másodpercig érvényes változatlan
pillanatképet kap; új transzfer kapcsolatonként legfeljebb három másodpercenként.
A master nem végez SD I/O-t a GATT callbackben.

A slave 30 másodpercenként frissít; 30 másodperces átviteli határidő és hiba
után 5 másodperces újrapróbálkozás védi az erőforrásokat. A teljes kép
transzferazonosító-, lapsorrend-, hossz-, CRC32-, számérték-, helyi dátum- és
korvizsgálat után, egyetlen mutex alatti cserével válik láthatóvá. Részleges
vagy hibás kép soha nem jelenik meg. A statisztikai hiba nem bontja az élő BLE
kapcsolatot, és a profilolvasással nem indít párhuzamos GATT-eljárást.

Az utolsó jó, aznapi kép RAM-ban marad; megszakadt kapcsolatnál vagy 90 másodperc
után elavultságjelzés látszik. Első szinkronig a napi kártyák `--` értéket mutatnak,
nem hamis nullát. Napváltáskor a tegnapi kép nem használható maiként, másik masterre
váltáskor a gyorsítótár érvénytelen. A statisztikát nem írjuk flashbe: újraindítás
után a mastertől töltődik vissza. Régi, 1005 nélküli master mellett az élő adat
működhet, de statisztika nem lesz; a mastert is frissíteni kell.

Az éjféli automatikus OTA SD nélkül is működik, ha van örökölt internetkapcsolat.
A rollback-validációhoz SD helyett teljes, friss master-statisztika kell a többi
Slave indulási feltétel mellett. Ez nem lazítja a fő kijelző SD-ellenőrzését.

## Közös kapcsolódás és időszinkron

A slave az első megtalált, megfelelő UUID-t hirdető masterhez kapcsolódik.
Először titkosított párosítást végez, majd hosszú GATT-olvasással átveszi a 1004
Wi-Fi/OTA profilt. Csak sikeres, tartós NVS-mentés után dolgoz fel órát/adatot.
Felderíti az energia-, időjárás- és órakarakterisztikát, feliratkozik mindháromra,
és azonnali kiolvasást is kér. A **1003 és 1004** karakterisztika nélküli régi master
nem kompatibilis ezzel a PIN-es Slave móddal; előbb a mastert is frissíteni kell.
Kapcsolatvesztéskor törli a kijelzett érvényes mintát, és automatikusan újrakeres.
Legfeljebb 5 s-os keresési ablakot és 3 s-os újrapróbálkozási szünetet használ.
A felderítés/első adat határideje 15 s, a csatlakozásé 8 s; a párosításra külön
45 s-os indulási határidő jut, a megjelenő PIN-pad 28 s-ig vár bevitelt.

A Wi-Fi SSID/jelszó és az automatikus frissítés állapota a párosított mastertől
származik. A master statikus IP-jét, adminjelszavát, NTP/MQTT/Loxone beállításait
nem másoljuk át: a slave **DHCP-vel saját IP-t kap**. A webes felület és az OTA
ezen a saját Wi-Fi-kapcsolaton használható, a Bluetooth nem internetátjáró.
Wi-Fi-hiba nem állítja meg a slave kijelzését. Slave módban az Adat seed kapcsoló kikapcsolt és letiltott;
egyszerre fogadás/továbbítás nem engedett, így nem alakulhat ki adattovábbítási hurok.

Az óra a master UTC idejét és aktuális helyi UTC-eltolását követi, beleértve
a nyári/téli váltást. Az első sikeres BT óraszinkron előtt nem indul statisztika.
Egy heartbeat ugyanazt a korábbi energiamérést nem integrálja újra; a minta
eredeti időbélyege és kora marad érvényben. Az órát rendszeresen a heartbeat
frissíti, a BLE átvitel/cache késleltetése miatt ez nem precíziós időszolgáltatás.

A diagnosztika bal oldalán két oszlopban 14 adatcsoport látszik. Bluetooth esetén
szerep, saját cím, master címe vagy `ZoleeEnergy` név, keresés/hirdetés/kapcsolat,
slave jelerősség, master kapcsolatszám és NTP/BT időszinkron is megjelenik.
A master számlálója minden tényleges BLE-kapcsolatot számol, így egy telefonos
BLE scanner kapcsolata is beleszámít. A webes `/api/diagnostics` ugyanezt az állapotot adja.

## PIN-es párosítás és beállításöröklés

1. A masteren kapcsold be az Adat seed funkciót. A `DataSeedPin` opcionális,
   pontosan hat számjegyű szöveg, például `"012345"`; üresen véletlen, eszközön
   tartósan megjegyzett PIN generálódik. A vezető nullák számítanak.
2. A master **helyi diagnosztikáján** a „BT név / párosítási PIN” mezőből olvasd le
   a PIN-t. Nyomd meg a **Párosítás engedélyezése (2 perc)** gombot. Ez a webes
   Bluetooth-beállításokban is elérhető, a szokásos adminhitelesítéssel.
3. A slave kijelzőjén megjelenő számbillentyűzeten írd be a kódot, majd OK.
   Törlés, Mégsem, vezető nulla, maszkolt bevitel és időtúllépés támogatott.
   PIN-bevitel alatt a témalapozó/diagnosztikai gesztusok nem működnek.
4. A sikeres párosítási kulcsok NVS-ben maradnak: újraindításkor nincs új PIN-kérés
   vagy párosítási ablakigény. Új eszköz zárt ablakban nem párosítható.

Legfeljebb három bond tárolható; öt PIN-próba után újra meg kell nyitni az ablakot.
Kulcsot újrapárosításhoz csak szándékosan megnyitott masterablakban törlünk;
a slave hibás titkosítás-visszaállításkor eldobja a saját érvénytelen bondját,
így a következő próbán ismét PIN-nel kell hitelesíteni. **Master PIN-váltás
minden korábbi bondot töröl**, a már korábban átadott Wi-Fi-jelszót azonban nem
tudja visszavonni; elveszett slave esetén a Wi-Fi-jelszót is változtasd meg.

A slave 30 másodpercenként, védett kapcsolaton újraolvassa a masterprofilt.
Változatlan adat nem írja újra a flash-t. Módosult SSID/jelszó vagy auto-update
kapcsoló esetén tartós profilt ment, majd statisztikamentés és az OTA kizáró
zárjának megszerzése után újraindul. A master kapcsolóját mindkét irányban követi.
A profilt induláskor a helyi konfiguráció fölé alkalmazza; a kapcsolót a webes
Slave-beállításokban ezért nem lehet kézzel állítani. A mastertől nem kap admin-
jelszót: a slave webes hozzáférését külön védd, ha erre szükség van.

A PIN fizikailag leolvasható a masterről: a kijelzők fizikai hozzáférése bizalmi
határ. Nincs PIN/jelszó az eseménynaplóban vagy a nyilvános mérési csomagokban.
Az opcionális webes adminhitelesítést a masteren is célszerű beállítani:
üres adminadatokkal a helyi hálózaton a PIN-beállítás és a párosításnyitás nem védett.
Az NVS-tárolás önmagában **nem jelent flash-titkosítást**; fizikai kiolvasás ellen
Secure Boot/Flash Encryption külön gyártási beállítása szükséges. A személyes
NVS-profil nem kerül a repóba vagy automatikusan az SD-re.

### 1004 védett profil (102 bájt)

| Offset | Tartalom |
|---|---|
| 0–1 | ASCII `ZB` |
| 2 | Verzió: 1 |
| 3 | Master automatikus frissítése: 0/1 |
| 4–5 | SSID és jelszó bájthossza (1–32, illetve 0–64) |
| 6–37 | SSID, nullával kitöltve |
| 38–101 | Wi-Fi-jelszó, nullával kitöltve |

Nincs szükség nagy MTU-ra: a kliens sorrend- és méretellenőrzött hosszú ATT-olvasást
végez. Titkosítatlan olvasáskor a GATT jogosultság és a callback is elutasítja a kérést.

### Hardveres párosítási elfogadási próba

- Mindkét eszközön az új firmware fusson, megfelelő C6 Bluetooth HCI-támogatással.
- Zárt ablak, hibás PIN, lejárt bevitel és titkosítatlan 1004 olvasás legyen elutasítva.
- Nyitott ablakban helyes, vezető nullás PIN-nel történjen SC/MITM/bonding;
  újraindítás és kapcsolatvesztés után ne kelljen ismét PIN.
- A slave kapjon saját DHCP-címet. Master Wi-Fi/auto-update módosítását
  legfeljebb a következő profilolvasás és mentés utáni újraindítás alkalmazza.
- PIN-váltás után a korábbi slave új PIN nélkül ne kaphasson új hálózati profilt.
- Három valós slave, nagy témák és Wi-Fi/OTA együttes használata ne okozzon
  watchdogot, SD-mentési hibát vagy hostverem-kimerülést.

## Adatfolyam és frissesség

MQTT, HTTP és Loxone módban ugyanazt a validált állapotot osztjuk meg. A host
egy másodpercenként ellenőrzi az állapotot; új minta vagy frissességváltás esetén
értesít, egyébként legfeljebb öt másodpercenként heartbeatet küld.
Feliratkozáskor azonnal elküldi az aktuális csomagot. A READ legfeljebb egy
másodperces cache-ből dolgozik, hálózati lekérést vagy SD-műveletet nem indít.

Mindhárom csomag pontosan 20 bájt: a kötelező, 23 bájtos ATT MTU is elegendő,
nincs szükség MTU-egyeztetésre vagy darabolásra. Minden több-bájtos mező
**little-endian**. Az előjeles számok kettes komplemens ábrázolásúak.
A protokoll verziója jelenleg **1**; ismeretlen verziót a slave ne értelmezzen.

### Energia csomag

| Offset | Típus | Tartalom |
|---|---|---|
| 0 | u8 | Protokollverzió: 1 |
| 1 | u8 | Flag: bit0 van minta, bit1 lejárt/nincs adat, bit2 éjszakai mód |
| 2 | u8 | Status: 1 normál, 2 felkészülés, 3 szigetüzem, 4 hiba, 5 hálózat újra elérhető |
| 3 | u8 | Aktuális nappali/éjszakai fényerő, % |
| 4 | i16 | PowerGrid, W; pozitív vételezés, negatív visszatáplálás |
| 6 | i16 | PowerBat, W; pozitív kisütés, negatív töltés |
| 8 | i16 | PowerPV, W |
| 10 | i16 | PowerFull, W |
| 12 | u16 | SocBat, százalék × 100 |
| 14 | i16 | BatteryTemperature, °C × 100 |
| 16 | u16 | Minta kora, másodperc; maximum 65535, minta nélkül 65535 |
| 18 | u16 | Pillanatkép sorszáma, 65535 után 0 |

Minta nélkül a mérési mezők nullák, de **nem valódi nulla mérésként** értelmezendők.
Lejárt mintánál a legutolsó értékek megmaradnak, a bit1 jelzi az érvénytelenséget.
A lejárat MQTT/HTTP módban a konfigurált timeout, Loxone módban 45 másodperc.
A slave saját, monotón órával felügyelje a BLE-adatérkezést is: például 15 s
heartbeat-kimaradásnál mutasson kapcsolathibát akkor is, ha a kapcsolat még él.

### Időjárás csomag

| Offset | Típus | Tartalom |
|---|---|---|
| 0 | u8 | Protokollverzió: 1 |
| 1 | u8 | Az alább felsorolt érvényességi/állapotbitek |
| 2 | i16 | Hőmérséklet, °C × 100 |
| 4 | u16 | Szél, km/h × 100 |
| 6 | u16 | Nap azimut, fok × 10 |
| 8 | i16 | Nap magasság, fok × 100 |
| 10 | u16 | Napkelte, helyi éjféltől perc |
| 12 | u16 | Napnyugta, helyi éjféltől perc |
| 14 | u32 | Minta UTC Unix-időbélyege, másodperc; 0 = nincs |
| 18 | u16 | Ugyanannak a pillanatképnek a sorszáma |

Flag bit0: hőmérséklet érvényes; bit1: szél érvényes; bit2: csapadékadat
érvényes; bit3: van csapadék; bit4: napsugárzásadat érvényes; bit5: van
napsugárzás; bit6: nappályaadatok érvényesek. Bit7 és a többi fenntartott bit 0.
Érvényességi bit nélkül a megfelelő numerikus mező nem használható.

Az értesítések nem garantált kézbesítésűek. Az energiaábra önmagában az energia
csomagból kirajzolható. Két csomag közös felhasználásához azonos sorszámot kell
megvárni; hiány esetén READ vagy a következő heartbeat állítja helyre az állapotot.
Két külön READ között frissülhet a cache, ezért ilyenkor is ellenőrizendő a sorszám.
Nincs szükség teljes sorszámfolytonosságra: a cache értesítés nélkül is frissülhet.

### Óra csomag (1003)

| Offset | Típus | Tartalom |
|---|---|---|
| 0 | u8 | Protokollverzió: 1 |
| 1 | u8 | Bit0: a master órája szinkronizált; egyéb bitek 0 |
| 2 | u32 | Jelenlegi UTC Unix-idő, másodperc |
| 6 | u16 | Másodpercen belüli ezredmásodperc, 0–999 |
| 8 | i16 | Aktuális helyi UTC-eltolás, perc; Budapest télen +60, nyáron +120 |
| 10 | u16 | Master energiaforrás timeoutja, másodperc, 5–3600 |
| 12–17 | 6 bájt | Fenntartott, 0 |
| 18 | u16 | Az energia/időjárás pillanatkép sorszáma |

Érvénytelen, nem szinkronizált vagy 2020–2100 közötti tartományon kívüli óra
nem állíthatja át a slave idejét. A slave az azonos sorszámú három csomagot
illeszti össze; adat/idő heartbeat 15 s-os kiesésekor bont és újrakeres.
A 1001/1002 csomag formátuma változatlan, így a korábbi kétkarakterisztikás
olvasók továbbra is használhatják a master energia- és időjárásadatait.

## P4/C6 és ellenőrzés

A P4 saját Bluetooth rádióval nem rendelkezik. A rögzített ESP-Hosted **1.4.7**
NimBLE VHCI támogatását használjuk a már működő SDIO kapcsolaton; a komponens
verzióját és a Wi-Fi javításait nem változtatjuk meg. A C6-on **BLE vezérlőt és
HCI over SDIO/VHCI kapcsolatot támogató firmware** szükséges. Az ESP-Hosted
1.4.7 C6 alapbeállítása tartalmazza ezt, de a készülék ténylegesen telepített
C6 firmware-ét hardveres próbával kell ellenőrizni.

Az ESP-IDF 5.5 által elvárt `ble_transport_ll_deinit` hookot az alkalmazás
biztosítja: a megosztott SDIO buszt BLE-hibánál sem állítja le, mert a Wi-Fi
továbbra is használja. A NimBLE mempool runtime-allokációja kikapcsolva marad,
mert annak új felszabadítási API-ját az ESP-Hosted 1.4.7 még nem támogatja.

Ha a hirdetés nem indul el, az eseménynapló/soros monitor vizsgálandó; a
`BLE adat seed aktív: ZoleeEnergy` esemény csak a hirdetés sikeres indításakor
keletkezik. A `BLE adat seed indul` sor önmagában még nem jelent működő rádiót.
A P4 OTA-frissítése nem frissíti automatikusan a C6 firmware-ét. A C6-ot nem
írjuk át automatikusan; szükség esetén külön, a panelhez illesztett eljárás kell.

Kikapcsolva nem indul NimBLE task/hirdetés. A BLE kód ettől még a firmware
része, ezért ez funkcióbővítés, nem méretsemleges formázás. A host dinamikus
pufferei PSRAM-ot használnak; a meglévő 32 KiB-os DMA-tartalék változatlan.

Hardveres átvételi próba:

1. Kikapcsolt kapcsolóval ne legyen ZoleeEnergy hirdetés; régi konfigurációval se.
2. Bekapcsolás és mentés után BLE scannerben szolgáltatás és név látszódjon,
   párosítás nélkül legyen READ és NOTIFY, az adatok egyezzenek a fő kijelzővel.
3. Ellenőrizendő az import/export, akkutöltés/kisütés, 0 W, SOC, Status,
   nappali/éjszakai fényerő, valamint hiányzó és valódi nulla időjárási adatok.
4. Adatforrás leállításakor lejárt jelzés, újraindításakor helyreállás történjen.
5. Slave bontása/újraindítása, több slave, hatótávvesztés után újracsatlakozás.
6. Wi-Fi újracsatlakozás, webes beállítások és nagy témák lapozása mellett is
   ellenőrizendő az adatfogadás, memória és folyamatos megjelenítés.
7. Kikapcsolás és mentés után szűnjön meg a BLE hirdetés és az adatszolgáltatás.

A host automatizált tesztje a bináris csomagolást, előjeleket, skálázást,
opcionális adatokat, timeoutot és a konfigurációs kapcsoló validálását ellenőrzi.
A fizikai rádió, hatótáv és több eszköz stabilitása csak valódi hardveren igazolható.

A dokumentált automatizált eredmények, az SD-s/SD nélküli elfogadási próbák és
a későbbi kis asztali kijelző fejlesztési határai az
[átadási jegyzetben](master-slave-handover.md) találhatók.
