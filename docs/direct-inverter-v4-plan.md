# Közvetlen inverter – v4.0.0 megvalósítás és átvétel

A v4 fejlesztésének első kérdése az volt, hogy a 10,1 hüvelykes Master meglévő szolgáltatásai mellett marad-e elég memória egy inverterkapcsolathoz. Ez a lap megőrzi a v3.4.0 összehasonlítási alapját, a megvalósítás fő döntéseit és a fizikai átvétel feltételeit. A profilválasztás és a telepítői beállítások a [kezelési útmutatóban](direct-inverter.md) találhatók.

## Kiinduló kapacitás és ellenőrzési bázis

A v3.4.0 kiadási fájlok és az `e0981f2` main commit partíciós táblái alapján mindkét célon volt szabad hely a következő firmware-kép számára. [v3.4.0 kiadás](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases/tag/v3.4.0)

| v3.4.0 cél | OTA-kép | Egy OTA-hely kapacitása | Fennmaradt hely |
| --- | ---: | ---: | ---: |
| Master 10,1″ | 3 679 648 B | 8 388 608 B | 4 708 960 B |
| Slave 4B | 3 590 128 B | 12 582 912 B | 8 992 784 B |

A panelekben 32 MiB külső PSRAM van. Egy korábbi, fizikai Loxone Audio-próbán 20 795 B szabad belső RAM és 10 995 B szabad DMA-képes memória maradt. A legnagyobb összefüggő szabad belső blokk 7 936 B volt. Ez indokolta a meglévő munkafeladatok újrahasználatát. A v4 inverteres terhelés alatti memóriatartalékot külön készülékmérés adja meg. [Teljesítmény és korábbi panelmérések](performance.md)

Az alapcommit helyi regressziója sikerült. A gazdagépen a ptrace korlátozása miatt a LeakSanitizer ki volt kapcsolva, az upstream LVGL JPEG és a WebSocket TX opcionális próbája kimaradt. Az alapkiadás CI-futása a két ESP-IDF 5.5.4 firmware-t is lefordította. A v4 aktuális fordításai, tesztjei és méretjelentései a fejlesztési PR ellenőrzéseihez tartoznak. [Alapcommit CI](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/44083654440) · [v4 fejlesztési PR](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/pull/52)

## Megvalósított üzemmód és erőforrások

A 10,1″-es Master a `DataReceptMode: "inverter"` üzemmódban egy aktív profilt olvas a meglévő, 10 KiB-os főfeladat vermén. Az Open-Meteo lekérése a meglévő OTA-háttérfeladaton fut. Az inverter HTTPS-lekérdezése, az időjárás és az OTA egyezteti a HTTPS-használatot, hogy a kézfogások memóriaigénye elkülönüljön. A hálózati inverterkeresés a webfelületről, kérésre indul. A keresés 8 KiB-os ideiglenes verme először az RTC-memóriából, szükség esetén a belső RAM-ból foglalható. A belső vezérlőblokkhoz mindkét esetben kell hely. A kész keresőfeladatot a főfeladat következő ciklusa vagy a következő keresés szabadítja fel. A többi üzemmód a saját adatforrását indítja. [Üzemmódok felépítése](architecture.md) · [Teljesítmény és memória](performance.md)

Inverter módban a kijelző energiaábrája, statisztikája, helyi adminfelülete és a bekapcsolt BLE-adatmegosztás működik. A Loxone, az Otthon, az Audioserver és a kaputelefon szolgáltatásai ehhez az üzemmódhoz nem indulnak el. A 10,1″-es és a dedikált 4B Slave az inverteres Mastertől kap órát és mérési adatokat. A 4B csak az azt támogató Master hitelesített képességjelzése után készíti elő a kaputelefonos médiát. Ha egy korábban erre képes Master másik módra vált, a 4B az új képesség megerősítésekor felszabadítja a médiát. [Slave-párosítás és profilverziók](ble-data-seed.md)

## Indítás és meglévő telepítések

| Indulási helyzet | Működés |
| --- | --- |
| Új vagy gyári állapotra visszaállított panel | A felhasználó kiválasztja a Wi-Fi-t, védett hálózathoz az ékezetes és speciális karaktereket is kezelő billentyűzeten adja meg a jelszót. IPv4-cím megszerzésekor a választó erőforrásai felszabadulnak. A gyári időszerverrel végzett szinkron után a kijelző kiírja a helyi webcímet. A 4B saját beüzemelő oldalán a Slave szerepet kell megerősíteni. [Első beállítás](first-run-wifi.md) |
| 10,1″-es Master mentett konfigurációval | A készülék betölti az érvényes beállítást. Inverter módban az időszinkron után már megadható a profil, az ábra az első érvényes mérésből épül fel. Az MQTT, HTTP és Loxone mód saját indulási lépéseit követi. Az új Master-firmware véglegesítéséhez Wi-Fi-cím és időszinkron szükséges. [Indulás üzemmódonként](first-start.md#indulás-mentett-beállításokkal) |
| SD-csere, korábban beállított vagy párosított Slave | A 10,1″-es készülék használhatja a visszaállítható belső konfigurációt, a kártya nélküli Slave megőrzi a szerepét. A beállított Slave az órát és az OTA véglegesítéséhez szükséges időjelet a párosított Mastertől kapja. [SD-csere és Slave](sd-card.md) · [OTA-feltételek](ota.md#két-ota-partíció) |
| Gyári beállítások visszaállítása | A 10,1″-es kijelző webes kezdőlapján megerősített törlés csak az adott készüléket érinti. Az új beüzemelést a Wi-Fi-választó indítja. [Menthető, törölt és megőrzött adatok](backup.md#gyári-visszaállítás) |

## Adatkapcsolatok és elfogadott döntések

- **Támogatás és keresés.** Tíz invertercsaládhoz 22 profil tartozik. A 18 helyi profilból 16 kereshető. A SolaX Pocket HTTP és a Solplanet HTTPS kézi címmel állítható be. Négy további profil gyártói felhőt olvas. A webes találat külön jelzi, hogy egy adatgyűjtőt észlelt vagy az inverter mérését is ellenőrizte. A Modbus-profilok olvasási regiszterei egyenként módosíthatók és visszaállíthatók. Gépnév megadásánál a Master névfeloldással követi a DHCP-címváltozást. Közvetlenül elérhető helyi IPv4-címnél az első sikeres mérés után a megismert MAC-cím alapján keresheti újra az eszközt. [Támogatott modellek és keresés](direct-inverter.md)
- **Mérési idő.** A helyi kapcsolat alapüteme 5 másodperc. A Fronius Solar.web és a SolaXCloud v2 lekérdezése 5 percenként, az AISWEI-é 20 percenként, a Huawei FusionSolar történeti adataié óránként történik. A felhőből többször visszakapott azonos időbélyeg egy mérésnek számít. A kijelző a működő kapcsolat után bekövetkező kapcsolatvesztést és az elavult mérést külön jelzi. [Frissítési idő és mérési hiányok](direct-inverter.md#frissítési-idő-és-mérési-hiányok)
- **Érvényes és hiányzó adat.** Az érvényes termelés mérő vagy akku nélkül is megjelenhet. A közvetlen profilok teljesítménytartományának felső határa csatornánként 327,67 kW. A hálózati vételezés és az akku kisütése pozitív előjelű. A **Van akkumulátor** kapcsolóval jelölhető a telepített tároló. Az akku hőfoka minden módban opcionális. Hiánykor a témák elrejtik az értéket, a mért 0 °C megjelenik. [Profilonkénti adatmezők](direct-inverter.md#támogatott-invertercsaládok-és-adatgyűjtők) · [Hőmérséklet és időjárási határok](mqtt.md)
- **Helyszín és napszak.** A felhasználó GPS-koordinátát ad meg. A Master 15 percenként tölti le az időjárást és helyben számolja a nappályát. A Master és a párosított v4 Slave napnyugtakor vált éjszakai témára, napkeltekor nappalira. [Nappálya és időjárás](direct-inverter.md#nappálya-időjárás-és-éjszakai-mód)
- **Heti sötétítés.** A Slave alaphelyzetben örökli a Master heti programját, a saját eszközkártyáján ettől eltérő sávot is kaphat. Aktív időablakban az első érintés a napszakhoz tartozó fényerőre ébreszt. A következő érintés újraindítja az ébrenléti időt. Lejáratkor csak aktív időablakban sötétül vissza a kijelző. [Időprogram és érintéses ébresztés](direct-inverter.md#slave-ek-heti-sötétítés-és-érintéses-ébresztés)

A profilok leírásában külön szerepel a támogatott inverter- és modulváltozat, az elérési adatok, az olvasási protokoll, a mérő és az akkumulátor szerepe. A firmware meghajtóit rögzített vagy szintetikus mintaválaszokkal teszteltük. [Modbus-profilok](inverters/modbus.md) · [Helyi HTTP és gyártói API-k](inverters/http.md) · [Victron GX MQTT](inverters/victron-mqtt.md) · [SOFAR HYD modellek](inverters/sofar.md)

## Fizikai átvétel

A képméret és a linkerjelentés a flashben és a statikusan foglalt memóriában elfoglalt helyet mutatja. A futás közbeni terhelhetőség és az adott telepítés adathelyessége az alábbi próbákkal ellenőrizhető.

| Próba | Mit kell rögzíteni vagy összevetni? |
| --- | --- |
| 10,1″ Master és 4B indulás, módváltás, első beállítás | A két hardver saját OTA-képének mérete és partícióhatára. Bootkor, üres és mentett profillal rögzítendő a szabad belső RAM, a legnagyobb belső és DMA-képes blokk, a PSRAM és a feladatok szabad verme. Loxone és inverter között ellenőrizendő a szolgáltatások és a 4B kaputelefon erőforrásainak életciklusa. |
| Valódi inverter, mérő, akkumulátor és jogosult gyártói fiók | A termelés, a vételezés/betáplálás és a töltés/kisütés előjele, SOC, hiányzó mezők és mérési idő egyezzen a gyári monitorozással. Újraindított helyi inverter új DHCP-címén a kijelző ugyanahhoz az eszközhöz térjen vissza. |
| HTTPS, BLE és kezelőfelület egyidejű használata | Helyi és felhős mérés, TLS-kézfogás, időjárás, webes mentés, újracsatlakozás és OTA alatt mérendő a legkisebb szabad memória és a válaszidő. Ismételt témaváltás és bontás után újra rögzítendő a memória és a kezelhetőség. |
| Párosított 10,1″ és 4B Slave | Napkelte/napnyugta, éjfélen átnyúló program, érintéses ébresztés, fényerő, rádiókapcsolat elvesztése és helyreállása a valós panelen. Régi és v4-es párosítást a dokumentált képességekkel kell összevetni. |

A gazdagépes tesztek a protokollokat, állapotváltásokat és a firmware valódi LVGL-nézeteit ellenőrzik. A rádiós hatótáv, a panel tényleges fényereje, a telepített inverter válaszideje és a készülék szabad DMA-memóriája helyszíni mérés marad. [Protokolltesztek és helyszíni ellenőrzés](direct-inverter.md#mit-ellenőriztünk-az-integrációkban) · [Fejlesztői ellenőrzések](developer-checks.md)

## Kiadás és eredeti források

A v4.0.0 kiadási folyamata akkor indul el, amikor a projekt saját `feature/direct-inverter-v4` ágának PR-jét a main ágba egyesítik. A kiadás mindkét panel saját OTA-csomagját tartalmazza a sikeres kiadási ellenőrzések után. [v4.0.0 kiadási leírás](versions/v4.0.0.md)

- [Fronius GEN24 magyar kézikönyv](https://manuals.fronius.com/html/4204260315/hu.html) és [Solar.web Query API](https://www.fronius.com/hu-hu/solarweb-query-api/csatlakozzon).
- [Sungrow SH-RT / RT-20 kézikönyv](https://info-support.sungrowpower.com/product-materials/31d744b7-742d-4707-bd1d-d6ed665c733c.pdf).
- [SolaX helyi Modbus és kompatibilitási táblázat](https://kb.solaxpower.com/solution/detail/ff8080818407e2a701840a22dec20032) és [SolaXCloud API v2](https://global.solaxcloud.com/blue/4/user_api/2024/SolaXCloud_User_API_V2.pdf).
- [Huawei M1 kézikönyv](https://info.support.huawei.com/Energy/info/en_US/all/docView/EDOC1100163578) és [FusionSolar API-fiókok telepítői útmutatója](https://info.support.huawei.com/Energy/info/en_US/solar/docView/EDOC1100612098).
- [GoodWe Modbus TCP útmutató](https://community.goodwe.com/resources/goodwe/2021-12-14558424.pdf).
- [Victron GX Modbus kézikönyv](https://www.victronenergy.com/live/ccgx:modbustcp_faq) és [GX MQTT beállítások](https://www.victronenergy.com/media/pg/Cerbo_GX/en/configuration.html).
- [SolarEdge SunSpec műszaki leírás](https://knowledge-center.solaredge.com/sites/kc/files/sunspec-implementation-technical-note.pdf).
- [Deye adatgyűjtők](https://www.deyeinverter.com/product/accessory-monitoring-1/stick-logger.html) és a [konkrét Deye modellen kipróbált helyi keretezések](https://www.openhab.org/addons/bindings/solarman/).
- [Solplanet T sorozat magyar kézikönyv](https://solplanet.net/wp-content/uploads/2021/07/Manual_ASW3000-1000-T_HU.pdf) és a [felhős API hozzáférésének integrátori útmutatója](https://help.energyid.eu/en/apps/solplanet-or-aiswei/).
- [SOFAR HYD 5–20KTL-3PH kézikönyv, G3 regiszterjegyzék és SOLARMAN V5 protokollforrások](inverters/sofar.md#források).
- [Open-Meteo adatok](https://open-meteo.com/en/docs), [használati feltételek](https://open-meteo.com/en/terms) és [SolarCalculator](https://github.com/jpb10/SolarCalculator).
