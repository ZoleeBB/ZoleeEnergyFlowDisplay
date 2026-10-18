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
- Nincs párosítás, bonding, PIN vagy titkosítás. A közelben bárki olvashatja a méréseket.
  Konfiguráció, hálózati hitelesítő adatok és jelszavak nem szerepelnek a csomagokban.
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
felület Slave módban elrejti, illetve a Wi-Fi-t opcionálisként jelöli.
Az SD, a helyi sablonok és a statisztikai tárhely továbbra is szükségesek.

A slave az első megtalált, megfelelő UUID-t hirdető masterhez kapcsolódik.
Felderíti az energia-, időjárás- és órakarakterisztikát, feliratkozik mindháromra,
és azonnali kiolvasást is kér. A 1003 órakarakterisztika nélkül a régi master
nem kompatibilis ezzel a Slave móddal; előbb a mastert is frissíteni kell.
Kapcsolatvesztéskor törli a kijelzett érvényes mintát, és automatikusan újrakeres.
Legfeljebb 5 s-os keresési ablakot és 3 s-os újrapróbálkozási szünetet használ.
A felderítés/első adat határideje 15 s, a csatlakozásé 8 s.

Ha megtartod vagy megadod a Wi-Fi SSID-t, a webes beállítások és OTA is
elérhetők maradnak. Wi-Fi-hiba nem állítja meg a slave indulását vagy a kijelzést.
Üres SSID esetén nincs hálózati webes elérés; beállításváltás SD-n, frissítés USB-n
lehetséges. Slave módban az Adat seed kapcsoló kikapcsolt és letiltott;
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
| 2 | u8 | Status: 1 normál, 2 felkészülés, 3 szigetüzem, 4 hiba |
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
