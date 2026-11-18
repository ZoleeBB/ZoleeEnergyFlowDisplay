# Közvetlen inverter

Ebben a módban a Master maga olvassa az inverter termelési adatait. Egy Masterhez egy aktív kapcsolat választható. A 10,1 hüvelykes kijelző Slave módban és a külön 4B Slave a Mastertől kapja az adatokat Bluetooth-on. A Loxone-kapcsolat, az Otthon-kártyák, az Audioserver és a kaputelefon kívül maradnak ezen az adatúton.

## Első beállítás

1. Új, mentett konfiguráció nélküli készüléken válassz Wi-Fi-hálózatot a panelen. A jelszót az érintőbillentyűzettel adhatod meg. Az IP-cím megszerzése után a kijelző szinkronizál a gyári `hu.pool.ntp.org` szerverrel. A fekete beállítóképernyőn látható címet, például `http://192.168.1.80`, nyisd meg a helyi hálózat egyik böngészőjében. [A képernyők és a billentyűzet](first-run-wifi.md)
2. Az általános **Beállítások** oldalon válaszd a **Közvetlen inverter** adatfogadási módot, mentsd a beállítást, és alkalmazd a módváltást. Már működő Masteren is innen válthatsz. SD-s konfigurációhoz az üzemmód azonosítója `"DataReceptMode": "inverter"`.
3. Nyisd meg az **Inverter beállítások** fület. Válaszd ki a gyártót és a kapcsolatot a közös listából. Helyi kapcsolatnál a gépnevet vagy IPv4-címet add meg, a portot külön mezőbe írd. Modbus esetén a szerelő által beállított eszközcím, gyártói felhőnél a megfelelő API-hozzáférés szükséges.
4. A **Van akkumulátor** jelölőt olyan telepítésnél kapcsold be, ahol az inverter akkumulátort kezel. Kikapcsolva a kijelző elhagyja az akkumulátorméréseket. Az akku hőmérséklete minden adatfogadási módban opcionális. A hiányzó érték és a hozzá tartozó felirat vagy ikon eltűnik, míg a mért 0 °C megjelenik.
5. Állítsd be az igényelt heti sötétítést és a párosított Slave-eket. Az oldal végén add meg a ház GPS-szélességét és hosszúságát tizedes fokban. Példa Budapesthez: **47.4979, 19.0402**. Az első szám a szélesség (−90…90), a második a hosszúság (−180…180).
6. Mentsd a profilt. A szinkronizálási állapot és a legutóbbi valódi mérés kora a fülön ellenőrizhető. Az elérési adatok cseréjekor add meg az új célhoz tartozó kulcsokat is. A helyi mentés a Master belső NVS-tárában marad meg újraindításkor. Az azonos kapcsolathoz tartozó változatlan jelszót a webfelület üresen hagyhatja.

### Modbus-regisztercímek módosítása

A **Modbus olvasási regiszterek** az inverterkapcsolat kiválasztása után jelennek meg. Minden mérésnél látható az aktuális és a gyári cím, a funkciókód, a 16 bites szavak száma és az adatformátum. A mezőbe tízes számrendszerű, nullától számozott PDU-cím kerül. SunSpec-modellnél a felület külön jelzi a modellhez viszonyított eltolást. A 0 érvényes érték. A több szóból álló mérés utolsó regiszterének is bele kell férnie a címtartományba.

Módosítsd a kívánt címet, majd válaszd az **Inverter és időprogramok mentése** gombot. A **Gyári cím visszaállítása** csak a kiválasztott sort állítja vissza, a visszaállítást is menteni kell. A módosítás azt határozza meg, honnan olvas a kijelző. A kijelző csak olvasási kérést küld az inverternek.

## Támogatott invertercsaládok és adatgyűjtők

| Inverter | Használható kapcsolat | Telepítéskor ezt ellenőrizd |
| --- | --- | --- |
| Fronius Primo/Symo GEN24 Plus | Helyi Solar API HTTP, Solar.web Query API | Engedélyezett Solar API vagy külön Solar.web API-kulcs. Ház és hálózat megjelenítéséhez Smart Meter. |
| Sungrow SH5.0–10RT, RT-20 | Helyi Modbus TCP | Az inverter megfelelő LAN-portja, Modbus-eszközcíme, mérő és a telepített akkumulátor. |
| SolaX X3-Hybrid G4 és X3-HYB-G4 PRO | Helyi Modbus TCP, kompatibilis Pocket HTTP, SolaXCloud v2 | Modbusnál válaszd ki a pontos kivitelt. A külön PRO beállítás az inverter protokollverzióját és a két- vagy három-MPPT-s kiosztást is figyelembe veszi. A helyi Pocket HTTP profil a hagyományos G4-hez készült. |
| Huawei SUN2000 L1/M1, köztük 5KTL-M1 | Helyi Modbus TCP, FusionSolar Northbound | WLAN-FE Smart Dongle és telepítő által engedélyezett Modbus TCP, illetve külön API-fiók. A helyi adatgyűjtő egyidejű klienskorlátjára ügyelj. |
| GoodWe ET/EH | Helyi Modbus TCP | A megfelelő adatgyűjtő támogassa a Modbus TCP-t. A Wi-Fi kapcsolat önmagában a gyártói felhő elérését is jelentheti. |
| Victron MultiPlus/Quattro GX rendszerrel | GX Modbus TCP vagy helyi MQTT | A GX-ben engedélyezd a választott szolgáltatást. MQTT-hez a VRM Portal ID kell. [GX MQTT beállítása](inverters/victron-mqtt.md) |
| SolarEdge SetApp | SunSpec Modbus TCP | Engedélyezett TCP-szerver, helyes port és eszközcím. A mérő szerepe dönti el, melyik pontot méri. |
| Deye SUN-SG, SG04LP3 háromfázisú sorozat | SOLARMAN V5, LSE-3, nyers RTU TCP-n vagy Modbus TCP átjáró | Válaszd az adatgyűjtő saját keretezését. V5 esetén az adatgyűjtő decimális sorozatszáma kell. |
| Solplanet ASW8000-T | Kompatibilis stick helyi HTTP/HTTPS, Modbus TCP–RS485 átjáró, AISWEI Cloud | Az ASW8000-T string inverterhez az akkujelölő kikapcsolható. Az RJ45 kivitelű RS485-porthoz megfelelő átjáró kell. A termelés külön mérő nélkül is megjelenhet. |

A [helyi Modbus-kézikönyv](inverters/modbus.md) modellenként felsorolja a portokat, regisztereket és mérőfeltételeket. A HTTP-profilokhoz szükséges mezők és jogosultságok a [helyi HTTP és a gyártói felhők útmutatójában](inverters/http.md) találhatók. A kompatibilitást az inverter, az adatgyűjtő és annak firmware-e együtt határozza meg. Az adott telepítésen a termelést és az előjeleket a gyári monitorozóval érdemes összevetni. A kijelzőn a pozitív hálózati teljesítmény vételezés, a pozitív akkumulátorteljesítmény kisütés.

## Frissítési idő és mérési hiányok

| Forrás | Alapütem |
| --- | --- |
| Helyi inverter | 5 másodperc |
| SolaXCloud v2 és Fronius Solar.web | 5 perc |
| AISWEI lakossági API | 20 perc |
| Huawei FusionSolar történeti eszközadat | 60 perc |
| Open-Meteo | 15 perc |

A felhőknél a kijelző a szolgáltatói mérés időpontját használja. Ismételten kiadott azonos mérés egyetlen mintának számít. A mérő nélküli rendszer részleges adatokat adhat. A szünetelő energiafolyam megőrzi az utolsó mérés korát, és a hosszabb kimaradásból nem készül becsült kWh. Az időjárás és a nappálya külön frissül. A beállítás újramentése a helyi forrást azonnal újraolvassa, azonos felhőfiók esetén a szolgáltatói ütem érvényesül.

## Nappálya, időjárás és éjszakai mód

A Master a koordinátákból és szinkronizált órájából helyben számolja a Nap irányát, magasságát, a napkeltét és a napnyugtát. Ellenőrizd a kijelző telephelyének időzónáját. A Master és a Slave éjszakai témája a napnyugta percében bekapcsol, a napkelte percében nappalira vált. A felhős időjárás kiesése ezt a számítást nem érinti. A számítás alapja a beépített, MIT-licencű [SolarCalculator](https://github.com/jpb10/SolarCalculator/tree/d7fb0005e3fbe288348c372c6adb76614ab2e254).

A Master a két koordinátát küldi az Open-Meteónak. A válaszból a külső hőmérséklet, páratartalom, szélsebesség, csapadék és közvetlen napsugárzás használható. A csapadékjelzés a környék modellezett időjárását mutatja. 45 percnél régebbi adathoz a kijelző elrejti az időjárási értékeket. A publikus végpont a szolgáltató ingyenes, nem kereskedelmi feltételeivel használható. A mérések forrásmegjelölése **Weather data by Open-Meteo**, az adatok licence CC BY 4.0. [Időjárási API](https://open-meteo.com/en/docs) · [Felhasználási feltételek](https://open-meteo.com/en/terms)

## Slave-ek, heti sötétítés és érintéses ébresztés

A Master általános beállításaiban engedélyezd a BLE-adatmegosztást, majd párosítsd a kijelzőket. Az **Inverter beállítások** fülön a párosított eszközök saját nevet, tájolást és külön nappali vagy éjszakai fényerőt kaphatnak. A teljes v4 funkciókhoz frissítsd a Mastert és a Slave-eket a saját hardverükhöz tartozó firmware-rel. [BLE-párosítás](ble-data-seed.md)

Az időprogram naponként egy kezdő és egy záró időpontot fogad. Például a hétfő **22:00–06:00** sáv hétfő este indul és kedd reggel ér véget. Egy bekapcsolt nap két azonos végpontja az egész napot lefedi. A sáv kezdete beleszámít, a vége már kívül esik. Alapból minden Slave a Master programját örökli. A saját időprogram kikapcsolása visszaállítja az öröklést.

Aktív időablakban a panel elsötétül. Az első érintés a pillanatnyilag érvényes nappali vagy éjszakai fényerőre ébreszti. Az újabb érintések újraindítják az ébrenléti időt. Lejáratkor csak a még aktív időablakban sötétül vissza. Így például a napnyugtakor megjelenő éjszakai téma estig látható maradhat, miközben a teljes sötétítést csak 22 órától kéred.

## Hibakeresés

| Jelenség | Mit nézz meg? |
| --- | --- |
| A panel a beállítási címet mutatja | Nyisd meg a kiírt HTTP-címet azonos hálózatról és mentsd el az általános beállításokat. Ezután állítsd be a kiválasztott üzemmódot. |
| Az inverter nem válaszol | Változat, port, IP-cím, protokoll és Modbus-eszközcím. Ellenőrizd a telepítői engedélyezést és az egyidejű klienskorlátot. |
| Csak a PV látszik | Nézd meg a műszaki útmutatóban, ad-e mérő-, ház- vagy akkuadatot a kiválasztott profil. Ellenőrizd a fizikai mérőt, a mérő szerepét és az akkujelölőt. |
| Régi a felhőmérés | Hasonlítsd össze a gyártói alkalmazás idejével. Ellenőrizd a régiós végpontot, API-fiókot, jogosultságot és kvótát. |
| Rossz irányba mozog az energia | Ellenőrizd a mérő helyét, irányát és a csatlakozási pont szerepét. Töltés, kisütés és ismert hálózati vételezés közben is hasonlítsd össze az értékeket. |
| Eltér a téma vagy a sötétítés időpontja | Ellenőrizd a hosszúság előjelét, a helyi időt, majd a heti időablakot és a Slave öröklési beállítását. |
