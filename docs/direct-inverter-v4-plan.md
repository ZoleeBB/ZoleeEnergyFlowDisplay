# Közvetlen inverter – v4.0.0 fejlesztési terv

Ez a dokumentum a fejlesztés követelményeit, a kiinduló méréseket és az átvételi feltételeket rögzíti. Az alábbi kompatibilitási lista fejlesztési cél. Az egyes készülékek mellett a későbbi használati útmutatóban külön szerepel majd a dokumentált, mintaválasszal ellenőrzött és valódi eszközön kipróbált működés.

## 1. Elbírja-e a kijelző?

Kiindulás a v3.4.0 kiadás és az `e0981f2` main commit. A [kiadás](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases/tag/v3.4.0) tényleges OTA-fájljai és a két projekt partíciós táblái alapján:

| Cél | OTA-kép | Egy OTA-hely kapacitása | Szabad hely |
| --- | ---: | ---: | ---: |
| Master 10.1 | 3 679 648 B | 8 388 608 B | 4 708 960 B |
| Slave 4B | 3 590 128 B | 12 582 912 B | 8 992 784 B |

A flash elegendőnek látszik. A 32 MiB külső PSRAM mellett a belső és a DMA-képes memória a kritikus. Egy korábbi Audio-próbán a szabad belső memória 20 795 B, a legnagyobb egybefüggő szabad blokk 7936 B, a szabad DMA 10 995 B volt. Ezek korábbi Loxone-mérések, az inverteres mód futásidejű tartaléka még nem ismert. A Masterben a boot után a 10 KiB-os main verem Loxone módban Audiot futtat, más módokban jelenleg várakozik. A kiválasztott inverter kezelése ezen a meglévő vermen vizsgálandó. [Teljesítmény és korábbi hardvermérések](performance.md)

Egy aktív inverterkapcsolat van Masterenként. Közös Modbus TCP, HTTPS és szükség esetén MQTT szállítás, kisméretű eszközleírásokkal. A nem kiválasztott driverek kódja flashben lehet, de külön taskot, kapcsolatot, válaszpuffert vagy folyamatos időzítőt nem kaphatnak. A webes felület inverteres erőforrásait is csak az érintett oldal kérheti. Az időjárás kis válaszát korlátos pufferrel kell feldolgozni. A DNS, TLS, tokenmegújítás, újracsatlakozás és sikertelen lekérdezés nem tarthatja fel a UI-t vagy a BLE-t. A felhős munka és az OTA egyidejű TLS-csúcsait mérni, szükség esetén sorosítani kell.

A 4B firmware jelenleg a Master azonosítása előtt előkészíti a JPEG-dekódert és elindítja a kaputelefont. A mód felismerését vagy a hitelesített Master képességeinek egyeztetését ezen foglalások elé kell helyezni. A visszacsatlakozás és a Master módváltása is felszabadítja a korábbi mód erőforrásait.

Az első futtatható változatnál mindkét bináris méretét és linker-térképét össze kell vetni a kiinduló állapottal. Panelen rögzítendő a szabad és legnagyobb belső/DMA blokk, PSRAM, tasklista és veremtartalék bootkor, beállítás nélkül, LAN-on, HTTPS-kézfogáskor, webes mentésnél, újracsatlakozásnál és BLE Slave + témaváltás/OTA mellett. A kapcsolatok ismételt létrehozása után a memória visszaállását is ellenőrizni kell. Hagyományos módban nem jelenhet meg új inverteres task vagy tartós inverterpuffer. Készülékmérés nélkül a futásidejű alkalmasság előzetes megállapítás.

A változatlan v3.4.0 alapregresszió a `python3 tests/run_tests.py` paranccsal sikeres (158 PASS-sor). Ebben a hostkörnyezetben a ptrace miatt a LeakSanitizer ki volt kapcsolva. Az upstream LVGL JPEG és a WebSocket TX opcionális ellenőrzése helyben kimaradt; az alapcommit [GitHub Actions futásában](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/44083654440) mind a 11 job zöld, a két ESP-IDF 5.5.4 buildet is beleértve. Ez az induló állapot igazolása.

## 2. Módválasztás és közös indulás

- A felület neve **Közvetlen inverter**, a mentett mód azonosítója tervezetten `inverter`. Régi konfiguráció és meglévő MQTT/HTTP küldő v4 alatt is használható.
- Egyeztetett döntés: **minden adatfogadási módban az időszinkron az egyetlen alkalmazásszintű indulási feltétel**. Masteren NTP, Slave-en a hitelesített Master órája. A Wi-Fi, illetve BLE ehhez szükséges adatút. A konfigurációs felület és a mérési adatok készenlétét külön kell kezelni. Az üres profil, az elérhetetlen adatforrás és a hiányzó első minta nem lehet végtelen bootképernyő oka.
- Ki kell dolgozni az indulást hiányzó/hibás SD-vel, a korábbi beállítások biztonságos helyreállítását, a gyári Wi-Fi-beállítást, valamint a háttértár nélkül használható funkciók körét. Ne íródjon felül hibásan olvasott beállítás, és a webes javítási út maradjon elérhető. A hardveres/OTA önellenőrzést és az alkalmazás használhatóságát külön kezeljük. Üres profil, elérhetetlen inverter vagy broker miatt az egészséges frissítés ne álljon vissza.
- Inverteres Masteren és hozzá kapcsolt Slave-en kaputelefonos kép/hang, SIP/RTP, Loxone, Otthon-kártyák és Audioserver nem indulhatnak. A képernyőt, helyi admint, időkezelést, statisztikát, karbantartást és a szükséges BLE-kapcsolatot meg kell tartani. Más módokban az inverteres kliens, felhős hitelesítés és Open-Meteo nem indul.

## 3. Képességek és adatéletciklus

- Külön kezelendő a tényleges nulla, a hiányzó adat, az elavult minta és a fizikailag hiányzó akkumulátor/mérő. Forrásonként ellenőrizendők az előjelek, W/kW, fázisösszeg, AC- és DC-oldali teljesítmény, pillanatnyi teljesítmény és összesített energia. A ház teljes fogyasztását csak arra alkalmas mérési topológiából szabad képezni.
- A **Van akkumulátor** jelző kikapcsolásakor az akkuadatokat nem kérjük és nem várjuk. Az akkuút, SOC, diagnosztika és a kizárólag ezekből számolt adatok megjelenítését ehhez kell igazítani. Mérő nélküli rendszernél legalább a termelés megjelenhet. Nulla hálózati teljesítmény önmagában nem bizonyít szigetüzemet.
- **Minden módban opcionális az akkumulátor-hőmérséklet.** Hiányzó kulcs, null vagy hiányzó párosítás/mérés elfogadható. Hiánykor minden témában eltűnik az érték és a hozzá tartozó díszítés; 0 °C érvényes adat. Ellenőrizni kell az MQTT, HTTP, Loxone, direkt inverter, BLE, előnézet és mindkét kijelző adatútját. A statisztika és az akkudiagnosztika csak valódi mintából dolgozhat.
- Egyeztetett döntés: helyi inverterolvasás **5 másodpercenként**. A gyártói felhő az engedélyezett ütemben kérdezhető, és a tényleges mérés idejét mutatjuk. Ugyanaz a forrás-időbélyeg egyszer kerül a statisztikába. Azonos teljesítmény későbbi, önálló mérésben érvényes adat. Forrásváltáskor és adatszünetben ne legyen téves energiaösszegzés. A lassú felhő nem használhatja változtatás nélkül az 5 másodperces LAN-forrás hibaküszöbét.

## 4. Inverterek és kommunikációs változatok

| Támogatási cél | Helyi adatút | További, korábban azonosított kapcsolat vagy feltétel |
| --- | --- | --- |
| Fronius Primo / Symo GEN24 Plus | Solar API, HTTP/JSON, rendszerszintű PowerFlow | Solar.web Query API, üzleti hozzáféréssel és használati díjjal. Smart Meter, akku és backup képesség a telepítéstől függ. |
| Sungrow SH5.0RT–SH10RT és RT-20 | Gyártói Modbus TCP az inverter LAN-portján | A LAN-port és a kommunikációs modul külön kezelendő. Típus és firmware szerint ellenőrizni kell az elérhető regisztereket. |
| SolaX X3-Hybrid G4, köztük a G4 Pro dokumentált konfigurációja | Pocket modulon Modbus TCP; kompatibilis változaton helyi HTTP | G4 Pro LAN, SolaXCloud v2 külön tokenes profil. A felhős élő nézet frissítési ígérete nem alkalmazható automatikusan a külső API-ra. |
| Huawei SUN2000 L1 / M1, kiemelten SUN2000-5KTL-M1 | SDongleA-05 WLAN-FE, engedélyezett helyi Modbus TCP | FusionSolar Northbound API engedélyezett fiókkal. Az egyetlen egyidejű Modbus kliens és a firmware-függő elérés legyen látható az útmutatóban. |
| GoodWe ET / EH | Gyártói Modbus TCP, kompatibilis LAN vagy Wi-Fi+LAN modul | Hagyományos Wi-Fi-modul támogatását csak igazolt protokollal és típussal szabad feltüntetni. |
| Victron MultiPlus / Quattro GX-szel | Cerbo GX / kompatibilis GX eszközön Modbus TCP vagy natív helyi MQTT | AC- és DC-csatolt PV, fázisösszesítés, hálózati mérés és rendszer-SOC a GX konfigurációja szerint. |
| SolarEdge SetApp | SunSpec Modbus TCP | Egy klienskapcsolat. A mérő és az akku eszközmodelljeit fel kell deríteni; eltérő mérőszerepeket nem szabad összeadni. |
| Deye SUN-SG, kiemelten SUN-12K-SG04LP3-EU | Kompatibilis adatgyűjtő SolarMAN V5 keretezéssel, illetve igazolt LSE-3 nyers Modbus-útvonal | Az adatgyűjtő hálózati protokollja és az inverter regisztertérképe külön azonosítandó. |
| TRACON / Solplanet ASW8000-T | Kompatibilis stick helyi HTTP/HTTPS, illetve az RS485-tel szerelt változat Modbus RTU–TCP átjáróval | AISWEI/Solplanet Cloud megfelelő API-hozzáféréssel. Termelés akkumulátor nélkül is; mérőadat csak igazolt eléréssel. Az RJ45 csatlakozó lehet RS485. |

Gyártónként külön leírás és rögzített, titokmentes mintaválasz szükséges. Dokumentálandó a telepítői engedély, firmware/modul, IP/port, eszközcím, regiszter- vagy API-verzió, egység/előjel, mérő/akku és lekérdezési ütem. A válaszokból vezessük le a kijelzett állapotokat. Ellenőrizendő esetek: időtúllépés, hibás vagy csonka keret, hibás modell, lejárt token, kvóta, újrainduló inverter, hiányzó mérő, töltés/kisütés, vételezés/betáplálás. A kijelző energiamegfigyeléshez olvas; invertervezérlő parancsokat nem igényel.

### Elsődleges kiinduló dokumentáció a driverekhez

- [Fronius GEN24 magyar kézikönyv](https://manuals.fronius.com/html/4204260315/hu.html) és [Solar.web Query API](https://www.fronius.com/hu-hu/solarweb-query-api/csatlakozzon).
- [Sungrow SH-RT / RT-20 kézikönyv](https://info-support.sungrowpower.com/product-materials/31d744b7-742d-4707-bd1d-d6ed665c733c.pdf).
- [SolaX helyi Modbus és kompatibilitási táblázat](https://kb.solaxpower.com/solution/detail/ff8080818407e2a701840a22dec20032) és [SolaXCloud API v2](https://global.solaxcloud.com/blue/4/user_api/2024/SolaXCloud_User_API_V2.pdf).
- [Huawei M1 kézikönyv](https://info.support.huawei.com/Energy/info/en_US/all/docView/EDOC1100163578) és [FusionSolar API-fiókok telepítői útmutatója](https://info.support.huawei.com/Energy/info/en_US/solar/docView/EDOC1100612098).
- [GoodWe Modbus TCP útmutató](https://community.goodwe.com/resources/goodwe/2021-12-14558424.pdf).
- [Victron GX Modbus kézikönyv](https://www.victronenergy.com/live/ccgx:modbustcp_faq) és [GX MQTT beállítások](https://www.victronenergy.com/media/pg/Cerbo_GX/en/configuration.html).
- [SolarEdge SunSpec műszaki leírás](https://knowledge-center.solaredge.com/sites/kc/files/sunspec-implementation-technical-note.pdf).
- [Deye adatgyűjtők](https://www.deyeinverter.com/product/accessory-monitoring-1/stick-logger.html) és a [konkrét Deye modellen kipróbált helyi keretezések](https://www.openhab.org/addons/bindings/solarman/).
- [Solplanet T sorozat magyar kézikönyv](https://solplanet.net/wp-content/uploads/2021/07/Manual_ASW3000-1000-T_HU.pdf) és a [felhős API hozzáférésének integrátori útmutatója](https://help.energyid.eu/en/apps/solplanet-or-aiswei/).

## 5. Webes konfiguráció, időjárás, nappálya

A **Loxone** fül helyén csak ebben a módban jelenik meg az **Inverter beállítások**. A sorrend: inverter és kapcsolatváltozat, ahhoz tartozó elérési/hitelesítési adatok, akkujelölő, kijelzők és időprogramok, végül földrajzi hely. Egyszerű első megoldás a szélesség és hosszúság közvetlen megadása. Határértékek és negatív koordináták ellenőrizendők, a nulla is lehet érvényes. Az alapértelmezett magyar időzónában a téli/nyári időváltást a meglévő időkezelés végzi. Mentés után a lemezre mentett érvényes beállítás lép életbe és azonnali első olvasást indít. Újramentés, hibás mentés, módváltás és forrásváltás közben nem maradhat párhuzamos régi kliens. A kulcsok/jelszavak nem kerülhetnek naplóba, diagnosztikába vagy böngészőbe visszaolvasható nyers formában.

Az időjárást a Master kéri az Open-Meteótól, **15 percenként**. Külső hőmérséklet, páratartalom, szél, csapadék és napsugárzás külön érvényességgel érkezik. A kijelzett esőjelzés környékbeli becslés. A koordinátákból és a pontos órából a napirány, a napmagasság, a napkelte és a napnyugta helyben számítható; az energiafrissítés, a nappálya és az időjárás hibája külön kezelendő. Az időjárási forrás megjelölése, licence és a nyilvános API használati feltételei a kézikönyvben és a webfelületen megjelennek.

Az adatmezők és az elérési feltételek forrásai az [Open-Meteo dokumentációja](https://open-meteo.com/en/docs), a [szolgáltatási feltételek](https://open-meteo.com/en/terms) és a [SolarCalculator forrása](https://github.com/jpb10/SolarCalculator).

## 6. Master, kétféle Slave, éjszakai megjelenés és sötétítés

- A 10.1-es Slave mód és a külön 4B firmware is kapjon időt, képességeket, friss energiaadatot, időjárást, nappályát és a saját profilját. Inverteres Master mellett egyik Slave-nek sem kell inverter- vagy időjáráskapcsolat. A kaputelefon-erőforrásokat már az indítási és újracsatlakozási úton is ki kell zárni.
- A jelenlegi párosított Slave-lista, eszközazonosság, fényerő, név és tájolás közös kezelőjét használja a Loxone és az inverteres fül. A Loxone-jelenlét UUID nem része az inverteres beállításoknak. A három meglévő Slave slot és a párosítás jogosultságai megmaradnak.
- A Master és Slave éjszakai témája napnyugtakor vált, napkeltekor nappalira tér vissza. Ettől független a heti időprogram, amely ténylegesen elsötétíti a kijelzőt. Éjfélen átnyúló sáv, hétváltás, óraátállítás, újraindítás, BLE-újracsatlakozás és a kapcsolat elvesztése szerepeljen a próbákban.
- A BLE formátum külön jelzi a módot és a mezők érvényességét. A régebbi Master/Slave viselkedését verzióegyeztetéssel kell kezelni; korábbi vevő ne kapjon hiányzó hőmérséklet helyett 0 °C-ot. Dokumentálni kell a fokozatos frissítést és azokat a képességeket, amelyekhez mindkét oldalon v4 szükséges.

## 7. Commitok és átadás

| Szakasz | Elkülönített commit, ellenőrzés |
| --- | --- |
| 1 | Ez a megvalósíthatósági terv, mért kiadási bázis, támogatási célok és döntési napló. |
| 2 | Üzemmód, közös indulás, webes konfigurálhatóság, szolgáltatás-életciklus és visszatöltés. Izolációs és régi konfigurációs regressziók. |
| 3 | Opcionális adatok minden bemeneten, statisztika, mindkét kijelző összes témája és BLE kompatibilitás. Nulla és hiány külön teszteset. |
| 4 | Közös adatút és helyi driverek, gyártónként elkülönített commitokkal és protokolltesztekkel. |
| 5 | Felhős adapterek gyártónként, kéréskorlátok, tokenek, mérési idő és ismételt válaszok tesztje. |
| 6 | GPS, időjárás és offline nappálya; éjszakai téma, heti sötétítés, eszközprofilok és visszacsatlakozás. |
| 7 | Részletes telepítési és hibakeresési útmutató, típusonkénti mátrix, fő README és v4.0.0 kiadási leírás. Csak a tényleges dokumentációs megjelenítési méretre kicsinyített képek kerülnek a repóba. |
| 8 | Teljes regresszió, szimulátorok és mindkét ESP-IDF build, OTA-csomag, méretkülönbség és hardveres ellenőrzés állapota. Végül PR a mainre. |

A meglévő kiadási folyamat automatikus kiadást az igazolt main merge-hez köt. A v4.0.0-t csak ezen saját repo megfelelő fejlesztési ágának azonosított PR-je válassza ki. Más PR, azonos nevű forkág, közvetlen main push, merge nélküli bezárás és ismételt CI nem hozhat létre ilyen új kiadást. Már létező v4.0.0 vagy újabb stabil verzió esetén a folyamat álljon meg. Mindkét, hardver szerint különböző OTA-bináris verzióját ellenőrizni kell.

## Döntési napló

1. Helyi inverter 5 másodpercenként, gyártói felhő a szolgáltatói ütemben, az utolsó valódi mérés korával. Elfogadva.
2. Az időszinkron utáni indulás minden adatfogadási módra érvényes. Elfogadva.
3. Az időprogram alatt az első érintés ébreszt, az aktuális napi/éjjeli fényerőre. További érintés újraindítja a beállítható időt. Lejáratkor csak akkor sötétül vissza, ha még aktív a heti időablak. Elfogadva.
4. A Master heti időprogramját minden Slave alapértelmezésben örökli. A Slave kártyáján önálló időprogram választható; az öröklés bármikor visszakapcsolható. Elfogadva.
