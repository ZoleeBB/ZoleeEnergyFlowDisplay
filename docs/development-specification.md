# Zolee Energy Flow Display – részletes fejlesztési specifikáció

> **2026-10-05 – elfogadott bővítés:** GitHub Release OTA/USB csomagok és választható MQTT/HTTP adatfogadás. A release workflow, OTA/USB-csomagolás és HTTP firmware-fogadó megvalósítva; a host regressziós tesztek és a firmware CI ellenőrzik. A HTTP Pico C minta és bekötési útmutató elkészült, Miniserver Gen 2 / 17.x hardveres átvétel még szükséges.

> **Státusz:** normatív fejlesztési specifikáció  
> **Célhardver:** Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1  
> **Célfelbontás:** 1280×800, fix fekvő tájolás  
> **Firmware alap:** ESP-IDF 5.5.4 + LVGL  
> **Elsődleges nyelv:** magyar  
> **Utolsó összefoglaló frissítés:** 2026-10-05

Ez a dokumentum a projekt során végigbeszélt igényeket, elfogadott döntéseket, viselkedési szabályokat, konfigurációs alapértékeket, UI-elvárásokat, MQTT/HTTP/Loxone integrációt, statisztikai számításokat, hibakezelést, OTA működést és vizuális követelményeket egyetlen specifikációban foglalja össze.

Ha a régebbi dokumentáció vagy egy korábbi implementáció ettől eltér, **ez a dokumentum tekintendő a fejlesztési cél elsődleges leírásának**, kivéve, ha egy későbbi, kifejezett felhasználói döntés módosítja.

---

# 1. Alap igény

A cél egy falra vagy állandó helyre szerelt, 24/7 üzemelő, önálló otthoni energiamonitor létrehozása, amely:

- valós időben megjeleníti a ház energiaáramlását;
- vizuálisan a jóváhagyott eredeti látványtervet követi;
- Loxone rendszerből a konfigurált mód szerint MQTT-n vagy közvetlen HTTP POST-tal kapja az adatokat;
- napi statisztikát számol és tárol;
- összehasonlítja az aktuális napot a tegnapi nap azonos időszakával;
- nappali és éjszakai megjelenést kezel;
- microSD-ről olvassa a konfigurációt és oda menti a statisztikát;
- Wi-Fi OTA frissítést biztosít;
- hálózati, MQTT-, SD- és konfigurációs hibák esetén jól elkülönített, egyértelmű állapotokat mutat;
- hosszú távú, felügyelet nélküli működésre alkalmas.

A kijelző elsődlegesen **információs kijelző**, nem vezérlőpanel.

---

# 2. Hardver

## 2.1. Célhardver

Fix célplatform:

**Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1**

A fejlesztés során használt célvariáns:

- 10.1 colos panel;
- Waveshare SKU: 33150;
- ESP32-P4;
- Wi-Fi társprocesszor;
- microSD foglalat;
- 32 MB Flash;
- PSRAM;
- érintőkijelző hardveresen jelen van, de a v1 felületen nem használjuk.

## 2.2. Felbontás és tájolás

A teljes alkalmazás:

- **1280×800**
- **fix fekvő tájolás**
- nincs automatikus forgatás;
- nincs álló mód.

## 2.3. Folyamatos üzem

A kijelző:

- 24/7 üzemben működik;
- nem kapcsol ki éjszakára;
- a fényerőt a NightMode alapján változtatja.

---

# 3. Szoftver stack

Kötelező technológiai alap:

- ESP-IDF
- **ESP-IDF 5.5.4**
- LVGL
- Arduino réteg nélkül
- Waveshare ESP-IDF BSP
- ESP-MQTT
- FatFs / microSD
- ESP-IDF HTTP server
- ESP-IDF OTA / rollback mechanizmus

A build cél:

`esp32p4`

A repository CI-jének ugyanezzel az ESP-IDF verzióval kell fordítania.

---

# 4. Repository és dokumentációs elvek

Repository:

`ZoleeBB/ZoleeEnergyFlowDisplay`

A projektben legyen legalább:

- `README.md`
- részletes konfigurációs dokumentáció;
- MQTT protokoll dokumentáció;
- Loxone dokumentáció;
- statisztikai dokumentáció;
- OTA dokumentáció;
- hibakezelési dokumentáció;
- hardver/build/flash dokumentáció;
- asset dokumentáció;
- ez a teljes fejlesztési specifikáció;
- minta MQTT payload;
- minta konfiguráció;
- Loxone Pico C program.

A valódi `config.json` nem kerül Gitbe.

A kód megjegyzései:

- magyarul készüljenek;
- a fontosabb modulokat, függvényeket és algoritmusokat érthetően dokumentálják;
- ne csak azt írják le, hogy „mit” csinál egy sor, hanem a fontosabb helyeken azt is, hogy „miért”.

---

# 5. Jóváhagyott vizuális irány

## 5.1. Elsődleges referencia

A vizuális alap **az első, világos, fehér kártyás „Otthoni energia” látványterv**.

Ennek fő jellemzői:

- világos, prémium jellegű dashboard;
- nagy lekerekített fehér kártyák;
- finom árnyékok;
- zöld és narancs hangsúlyszínek;
- bal oldalon napi összegző kártyák;
- középen nagy energiaáramlási vizualizáció;
- jobb oldalon grafikonok;
- alul horizontális összefoglaló sáv;
- jól kidolgozott, részletes ikonok;
- sok fehér tér;
- nem „mérnöki/debug UI”, hanem kész termék érzet.

## 5.2. Elutasított referencia

A később készített sötét, neon-jellegű dashboard **nem vizuális referencia**.

Az éjszakai téma sem ezt a stílust használja.

## 5.3. Pixelpontosság

A cél nem az, hogy „hasonló hangulatú” legyen, hanem hogy a hardver adottságai mellett **a lehető legpontosabban ugyanazt a vizuális karaktert és hierarchiát adja vissza**, mint a jóváhagyott első látványterv.

A végső finomhangolás valós kijelzőfotó alapján történik.

---

# 6. Grafikai assetek és ikonok

## 6.1. Minőségi követelmény

Egyszerű:

- LVGL karakterikonok;
- primitív kör/téglalap rajzok;
- kézzel összerakott nagyon egyszerű nap/felhő/ház piktogramok

**nem tekinthetők végleges vizuális megoldásnak**, ha láthatóan elmaradnak a látványterv ikonminőségétől.

## 6.2. Preferált irány

A felismerhető vizuális ikonokhoz részletes asseteket kell használni.

Elsődleges végleges irány:

- PNG asset;
- átlátszó háttér;
- a látványtervhez igazodó részletesség;
- finom árnyék;
- puha körháttér külön LVGL rétegen;
- egységes stílus.

SVG használható:

- forrásként;
- tervezési formátumként;
- ha a végeredmény vizuálisan teljesen megfelel.

Ha az SVG renderelés nem adja vissza pontosan a kívánt minőséget, PNG használata az elvárt megoldás.

### Újraalkotás és a körvonalak szétválasztása (2026-10-05)

A látványterv etalon, **képernyőképből kivágott ikon nem használható**.
Az ikonokat önálló, átlátszó hátterű grafikaként kell újraalkotni.
A korábban elfogadott újragenerált grafikákat meg kell őrizni; az elfogadott
nyolcikonos képlap elemeinek fájlokra bontása megengedett, ez nem a
látványterv kivágása. Az eredeti képek az `assets/source/`, a firmware PNG-k
az `main/assets/png/` könyvtárban vannak. Mind a 29 futásidejű ikon PNG,
beleértve az időjárási, kapcsolat- és speciális állapotokat is.

A középső csillagábra ház-, napelem-, hálózat- és akkumulátorikonja **nem
állandó színű kerettel együtt tárolt kép**. A körvonal külön natív LVGL objektum.
A keret pontosan az adott összekötő vonal és mozgó energiapontok színét kapja,
éjszakai tompítás esetén is ugyanazzal a színértékkel.

| Ág / állapot | Irány | Körvonal és áramlás |
| --- | --- | --- |
| Napelem termel | Napelem → központ | Zöld |
| Ház fogyaszt | Központ → ház | Narancs |
| `PowerGrid > 0` | Hálózat → központ (vételezés) | Narancs |
| `PowerGrid < 0` | Központ → hálózat (visszatáplálás) | Zöld |
| `PowerBat > 0` | Akku → központ (kisütés) | Narancs |
| `PowerBat < 0` | Központ → akku (töltés) | Zöld |
| `abs(Power) < 0,0005 kW` | Nincs áramló pont | Az összekötővel azonos semleges szürke |
| Szigetüzem, hálózati ág | Leválasztva, nincs pont | Szürke |

A SOC kizárólag a külön töltöttségi sávot és a numerikus értéket vezérli;
**az akkumulátor körvonalát nem színezheti át**. A központi hub saját ezüst
pereme a statikus hub-grafika része maradhat.

## 6.3. Tárolás

A PNG-ket **minden esetben a repository-ban kell tartani**.

Elsődleges implementáció:

- firmware-be ágyazott assetek, ha kényelmesen elférnek.

Megengedett fallback:

- microSD-ről betöltött PNG-k, ha később a Flash/OTA méret vagy memória indokolja.

A vizuális assetek rendelkezésre állása ne legyen indokolatlanul az SD működéséhez kötve, ha a firmware-ben kényelmesen elférnek.

## 6.4. Asset-kategóriák

Legalább az alábbiaknak kell kidolgozott assettel rendelkezniük:

- fejléc ház/levél logó;
- termelés;
- fogyasztás;
- hálózat;
- pénz;
- napelem;
- akkumulátor;
- ház;
- energia hub;
- CO2;
- fa;
- önellátás;
- normál állapot;
- áramszünetre felkészülés;
- szigetüzem;
- rendszerhiba;
- napos;
- részben felhős;
- felhős;
- eső;
- hó;
- éjszaka/hold;
- szél;
- Wi-Fi hiba;
- MQTT hiba;
- adat-timeout;
- első adatra várakozás.

## 6.5. Dinamikus elemek

A következők maradhatnak natív LVGL rajzolásúak:

- energiaáramlási vonalak;
- mozgó pontok;
- grafikonok;
- kártyák;
- keretek;
- SOC progressbar;
- aktuális időt jelző grafikonvonal.

---

# 7. Dashboard – általános elrendezés

A végleges felület egyetlen oldalas.

Nincs:

- tab;
- lapozás;
- „Teljesítmény / Nap / Hét / Hónap / Év” navigáció;
- dátumválasztó;
- menü;
- érintéses navigáció.

Mindig az aktuális nap látható.

A makro-elrendezés:

1. fejléc;
2. bal oldali nagy napi kártyák;
3. központi energiaáramlás;
4. jobb oldali két grafikon;
5. alsó összefoglaló sáv.

---

# 8. Fejléc

A fejléc tartalma:

- projekt ikon;
- projektcím;
- opcionális rövid alcím;
- dátum;
- időjárás ikon;
- külső hőmérséklet;
- szél ikon;
- szélsebesség;
- idő.

## 8.1. Projektcím

Konfigurálható.

Alapérték:

`Otthoni energia`

## 8.2. Idő

Formátum:

`HH:mm`

Másodperc nem jelenik meg.

## 8.3. Dátum

A dátum megjelenhet a fejlécben.

Formátum nyelvfüggő:

Magyar:

`2026.10.04.`

Angol:

`2026-10-04`

## 8.4. Időjárás

Normál nézetben csak:

- időjárás ikon;
- hőmérséklet;
- szél ikon;
- szélsebesség.

Nem kell kiírni például:

- „Napos”;
- „Felhős”;
- „Van napsugárzás”;
- „Nincs csapadék”.

## 8.5. Normál kapcsolati állapot

Normál működés közben nem kell állandó:

- Wi-Fi ikon;
- MQTT ikon.

Csak hiba esetén jelenjen meg kapcsolati figyelmeztetés.

---

# 9. Időjárás ikonlogika

A prioritás pontosan:

1. csapadék;
2. éjszakai mód;
3. napsugárzás;
4. PV alapján részben felhős;
5. felhős.

Szabályok:

- `HasPrecipitation = 1` és `Temperature < -4.0 °C`:
  - felhőből hulló hó;
- `HasPrecipitation = 1` és `Temperature >= -4.0 °C`:
  - eső;
- nincs csapadék és `NightMode = 1`:
  - hold / éjszakai ikon;
- nappal, nincs csapadék, `HasSunRadiation = 1`:
  - napos;
- nappal, nincs csapadék, `HasSunRadiation = 0`, `PowerPV > 1 kW`:
  - részben felhős;
- egyéb nappali eset:
  - felhős.

A csapadék éjszaka is elsőbbséget élvez a hold ikonnal szemben.

---

# 10. Központi energiaáramlás

## 10.1. Elemei

Négy fő energiaelem:

- napelem;
- ház;
- akkumulátor;
- hálózat.

Az elrendezés és vizuális karakter az eredeti látványtervet követi, központi energia hubbal.

## 10.2. Kapcsolati vonalak

A nulla teljesítményhez is tartozik:

- halvány szürke statikus kapcsolat.

Nulla esetén:

- mozgó pont nincs.

## 10.3. Mozgó energiapontok

A vonalakon mozgó pontok jelzik az energia irányát.

Sebesség:

- teljesítményarányos;
- 0…7 kW között skálázódik;
- **7 kW a maximális animációs sebességhez tartozó felső határ**;
- 7 kW felett a sebesség nem nő tovább.

Az animáció legyen folyamatos és sima.

## 10.4. Színek

PV termelés:

- zöld.

Hálózati export:

- zöld.

Hálózati import:

- narancs.

Akkumulátor töltés:

- zöld.

Akkumulátor kisütés:

- narancs.

Ház fogyasztás:

- narancs.

0 W:

- szürke statikus kapcsolat.

## 10.5. Előjel megjelenítés

A `PowerGrid` és `PowerBat` kijelzett pillanatnyi értéke:

- előjel nélkül jelenjen meg;
- az irányt a szín, az ikon és a pontanimáció mutatja.

## 10.6. Teljesítmény formázás

Ha:

`abs(value) >= 1 kW`

akkor:

- kW;
- 1 tizedes.

Ha:

`abs(value) < 1 kW`

akkor:

- egész W.

Példák:

- 7.14 kW → 7.1 kW
- 0.1 kW → 100 W
- 0.001 kW → 1 W

---

# 11. Akkumulátor

## 11.1. Pillanatnyi adatok

Megjelenítendő:

- SOC;
- pillanatnyi PowerBat;
- akkumulátor-hőmérséklet.

## 11.2. Napi adatok

Megjelenítendő:

- ma akkumulátorba töltött energia;
- ma akkumulátorból kivett energia.

Csak erre a két napi értékre kell tegnapi összehasonlítás.

Nem kell tegnapi összehasonlítás:

- SOC-ra;
- pillanatnyi PowerBat értékre;
- BatteryTemperature értékre.

## 11.3. SOC megjelenés

Az akkumulátor külön töltöttségi sávjának hossza és színe tükrözze a `SocBat` értéket. Az ikon állandó PNG; a körvonal az energiairányt követi (6.2. fejezet).

Szín:

- > 50%: zöld;
- 20…50%: narancs/sárga;
- < 20%: piros.

A `SocBat` numerikusan:

- 1 tizedes %.

## 11.4. Akkumulátor hőmérséklet

Megjelenítés egész °C-ban.

Szín:

- ≤ 40 °C: normál;
- > 40 °C és ≤ 50 °C: narancs;
- > 50 °C: piros.

---

# 12. Rendszerállapot

A közös JSON adatcsomag `Status` mező:

## Status = 1

Felirat:

`Normál működés`

Vizuál:

- zöld;
- pozitív/check jellegű ikon.

## Status = 2

Felirat:

`Felkészülés áramszünetre`

Jelentés:

- az akkumulátor 100%-ra töltése;
- szükség esetén hálózatról is;
- 100%-on tartás.

Vizuál:

- sárga/narancs;
- akkumulátor + villám jellegű ikon.

## Status = 3

Felirat:

`Szigetüzem`

Jelentés:

- hálózatról leválasztva;
- csak akkumulátor/PV oldalról működik.

Vizuál:

- kék;
- leválasztott hálózat / ház + akkumulátor jelleg.

A fő energiaáramláson:

- hálózat leválasztottnak látszik;
- nincs hálózati mozgó pont;
- a kapcsolat szürke / inaktív.

## Status = 4

Felirat:

`Hiba`

Vizuál:

- erős piros állapotjelzés.

Fontos:

`Status = 4` **nem kommunikációs hiba**, ezért:

- a dashboard nem tűnik el;
- nem lesz teljes képernyős kommunikációs hiba;
- a rendszerállapot kártya erősen piros jelzést kap.

---

# 13. Nappali és éjszakai mód

Forrás:

`NightMode`

## NightMode = 0

- világos téma;
- nappali fényerő.

## NightMode = 1

- pontosan ugyanaz az elrendezés;
- fekete/sötét háttér;
- visszafogottabb zöld/narancs/piros/kék hangsúlyok;
- kisebb kontraszt;
- éjszakai fényerő.

Alap fényerők:

- nappal: 80%;
- éjjel: 20%.

A felület elrendezése nem változhat témaváltáskor.

---

# 14. Napi kártyák

A teljes funkcionális mutatókészlet:

- Mai termelés;
- Mai fogyasztás;
- Hálózati vételezés;
- Visszatáplálás;
- Megtakarított pénz;
- Megtakarított CO2;
- Fa-egyenérték;
- Önellátás;
- Akkumulátor blokk;
- Rendszerállapot.

A kártyák vizuális megjelenése:

- részletes ikon;
- cím;
- „Ma összesen” jellegű segédszöveg, ahol értelmes;
- nagy szám;
- tegnapi összehasonlítás, ahol értelmes.

---

# 15. Tegnapi összehasonlítás

Az összehasonlítás mindig:

- tegnapi nap;
- ugyanazon **lezárt 5 perces határig**.

Példa:

14:37-kor:

- ma 00:00–14:35
- tegnap 00:00–14:35

kerül összehasonlításra.

Ha nincs használható tegnapi adat:

- összehasonlítási blokk ne jelenjen meg.

Ha tegnapi referenciaérték 0:

- százalék nem számolható;
- kijelzés: `—`

Ha ma > tegnap:

- zöld felfelé nyíl;
- pozitív százalék.

Ha ma = tegnap:

- zöld felfelé nyíl;
- `0.0%`

Ha ma < tegnap:

- narancs lefelé nyíl;
- negatív százalék.

---

# 16. Napi értékek formázása

Energia:

- 1 tizedes kWh.

Pénz:

- egész pénzegység;
- az egység konfigurálható szöveg.

CO2:

- 1 tizedes kg.

Fa-egyenérték:

- 2 tizedes.

Önellátás:

- százalék.

SOC:

- 1 tizedes %.

Külső hőmérséklet:

- 1 tizedes °C.

Szél:

- egész km/h.

Akkumulátor-hőmérséklet:

- egész °C.

---

# 17. Grafikonok

Mindig az aktuális nap grafikonjai.

Nincs:

- heti;
- havi;
- éves;
- korábbi napi böngészés.

## 17.1. Első grafikon

Sorozatok:

- `PowerPV`
- `PowerFull`

Színek:

- PV: zöld;
- fogyasztás: narancs.

## 17.2. Második grafikon

Sorozatok:

- `PowerGrid`
- `PowerBat`

Színek:

- Grid: piros;
- Battery: kék.

A nullavonal legyen hangsúlyos.

Előjelek:

`PowerGrid`

- pozitív: import;
- negatív: export.

`PowerBat`

- pozitív: kisütés;
- negatív: töltés.

## 17.3. Grafikon stílus

A látványtervhez igazodva:

- simított görbék;
- vékony vonal;
- finom rács;
- automatikus Y skála;
- 00:00–24:00 fix X tengely;
- a jövőbeli idő üres;
- aktuális idő finom függőleges jelölése;
- visszafogott áttetsző gradient fill.

A második, előjeles grafikon kitöltése a nullavonal irányába történjen.

---

# 18. Választható adatforrás

A `config.json` gyökérszintű `DataReceptMode` mezője választja ki az adatfogadást:

| Érték | Adatút |
|---|---|
| `mqtt` | Loxone → MQTT broker → kijelző; a meglévő működés |
| `http` | Loxone → HTTP POST → kijelző; közvetítő nélkül |

Hiányzó mező: `mqtt` (régi konfigurációk kompatibilitása). Az érték kis-/nagybetűtől függetlenül értelmezhető, belül kisbetűsre normalizálandó. Üres, rossz típusú vagy ismeretlen érték konfigurációs hiba, nem okozhat csendes módváltást.

Egyszerre egy adatforrás aktív; nincs automatikus fallback. A mód csak induláskor kerül beolvasásra, megváltoztatásához újraindítás szükséges. HTTP módban az MQTT-kliens nem indul el, a teljes `mqtt` konfiguráció elhagyható, annak hibás vagy hiányzó mezői sem blokkolják a HTTP működést. MQTT módban a korábbi broker- és hitelesítési szabályok maradnak.

A két fogadási út ugyanazt a validátort és közös állapot-/statisztikafeldolgozást használja. Nincs Loxone WebSocket, Modbus vagy közvetlen inverter API. A kijelző nem kérdezi le a Loxone HTTP API-ját: a Miniserver küldi a csomagokat.

## 18.1. MQTT-kapcsolat

Transport:

- plain MQTT TCP;
- TLS nélkül.

Alap port:

`1883`

QoS:

`1`

Topic alapérték:

`loxberry/mqttgateway/energymonitor/state`

A broker címe konfigurálandó.

A broker host/IP alapértéke:

- üres.

Felhasználónév és jelszó:

- opcionális;
- üres esetén anonim kapcsolat.

## 18.2. MQTT-publikálási periódus

Loxone oldalról:

- egy teljes JSON csomag;
- 5 másodpercenként.

Ajánlott:

- retained message.

A retained üzenet korát a firmware **nem** próbálja ellenőrizni.

Nincs payload timestamp mező.

## 18.3. HTTP POST adatfogadás

Célkörnyezet: Loxone Miniserver Gen 2, Loxone Config és firmware 17.x.

- Fix útvonal: `POST http://<kijelző-IP>/api/energy`, a meglévő HTTP szerver 80-as portján.
- Tartalomtípus: `application/json`; a `charset=utf-8` kiegészítés megengedett.
- A body egy teljes, 12 kötelező mezős JSON objektum; mezők, mértékegységek, előjelek és tartományok a 19–21. fejezet szerint.
- **Nincs adatfogadási hitelesítés:** sem Basic Auth, sem API-kulcs, sem token. A végpont akkor is jelszó nélkül fogad, ha az OTA/web felület jelszóval védett.
- Nincs GET-es adatküldés, query-paraméteres vagy mezőnkénti részleges frissítés.
- A body mérete legfeljebb 4096 bájt; üres vagy túlméretes kérés nem fogadható el. Korlátos fogadási idő és teljes body-olvasás szükséges; megszakított kérés nem frissíthet részadatot.
- Egy érvényes kérés egyszer frissíti az élő állapotot és a statisztikát. A fogadási időt a kijelző állítja elő; nincs kötelező payload timestamp.
- Hibás kérés nem módosíthat állapotot, statisztikát vagy az utolsó érvényes csomag időpontját.
- Az endpoint kiszolgálása nem várhat SD-írásra; hiba vagy párhuzamos webhasználat nem állíthatja le a UI-t.

| HTTP válasz | Jelentés |
|---|---|
| `200` + `{"ok":true}` | Az érvényes minta feldolgozása megtörtént |
| `400` | Üres/hibás JSON, hiányzó/dupla mező, hibás típus vagy tartomány |
| `405` | Nem POST módszer; `Allow: POST` fejléc |
| `408` | A kérés teljes body-ja határidőn belül nem érkezett meg |
| `409` | A HTTP adatforrás nincs kiválasztva (`DataReceptMode=mqtt`) |
| `413` | Túl nagy body |
| `415` | Nem támogatott tartalomtípus |
| `503` | A feldolgozás indulás miatt még nem kész, vagy átmenetileg nem elérhető |

A hibaválasz rövid JSON, gépi hibakóddal és szükség esetén mezőnévvel. Nem szabad a teljes méréstartalmat folyamatosan naplózni. A korábbi web/OTA végpontok hitelesítése és funkciói megmaradnak.

---

# 19. Közös MQTT/HTTP payload

Kötelező mezők:

```json
{
  "PowerGrid": -3.8,
  "PowerBat": -1.2,
  "PowerPV": 7.1,
  "SocBat": 82,
  "PowerFull": 2.1,
  "Temperature": 18.6,
  "WindSpeed": 12.3,
  "HasSunRadiation": 1,
  "HasPrecipitation": 0,
  "Status": 1,
  "NightMode": 0,
  "BatteryTemperature": 27
}
```

## 19.1. Mezők jelentése

### PowerGrid

kW

- pozitív: hálózati vételezés;
- negatív: visszatáplálás.

### PowerBat

kW

- negatív: akkumulátor töltés;
- pozitív: akkumulátor kisütés.

### PowerPV

kW

Aktuális PV termelés.

### SocBat

%

Akkumulátor SOC.

### PowerFull

kW

Teljes házfogyasztás.

### Temperature

°C

Külső hőmérséklet.

### WindSpeed

km/h

### HasSunRadiation

- 0
- 1

### HasPrecipitation

- 0
- 1

### Status

- 1
- 2
- 3
- 4

### NightMode

- 0
- 1

### BatteryTemperature

°C

---

# 20. Közös MQTT/HTTP validáció

Minden felsorolt mező kötelező.

A mezőnevek:

- kis-/nagybetű érzéketlenek.

Tehát például:

- `PowerGrid`
- `powergrid`
- `POWERGRID`

mind elfogadható.

Ugyanazon mező eltérő case változatban kétszeri megadása:

- hiba.

Ismeretlen extra mezők:

- elfogadhatók;
- figyelmen kívül hagyhatók.

Ha bármely kötelező mező:

- hiányzik;
- rossz típusú;
- NaN/Inf jellegű;
- tartományon kívüli;

akkor:

- **az egész csomag elutasításra kerül**;
- sem a képernyő;
- sem a statisztika

nem frissül belőle.

---

# 21. Közös MQTT/HTTP mezőtartományok

`PowerPV`

- -0.05…15 kW elfogadható;
- -0.05…0 negatív zaj → 0-ra normalizálva;
- < -0.05 → hibás;
- > 15 → hibás.

`PowerFull`

- -0.05…15 kW;
- -0.05…0 → 0;
- < -0.05 vagy > 15 → hibás.

`PowerGrid`

- -15…+15 kW.

`PowerBat`

- -15…+15 kW.

`SocBat`

- 0…100%.

`Temperature`

- -35…+50 °C.

`BatteryTemperature`

- -20…+80 °C.

`WindSpeed`

- 0…150 km/h.

`Status`

- csak 1…4;
- numerikus egész.

`NightMode`

- csak 0 vagy 1.

`HasSunRadiation`

- csak 0 vagy 1.

`HasPrecipitation`

- csak 0 vagy 1.

---

# 22. Adat-timeout

Mindkét módban az alapérték **30 másodperc**, az utolsó érvényes csomagtól, monoton idővel mérve. Első csomag nélkül a fogadó üzemkész állapotától számítjuk. MQTT-nél a meglévő `mqtt.data_timeout_seconds`, HTTP-nél a `http.data_timeout_seconds` beállítás érvényes; alapértékük és megengedett tartományuk azonos legyen.

Hibás csomag vagy egyszerű HTTP-kapcsolódás nem nullázza a timeoutot. Induláskor első adatra várakozó képernyő látszik, majd adat nélkül 30 másodperc után a kiválasztott módhoz tartozó teljes képernyős hibaoldal.

| Mód | Magyar főszöveg | Angol főszöveg |
|---|---|---|
| MQTT | `Nem érkezett MQTT adatcsomag.` | `No MQTT data received` |
| HTTP | `Nem érkezik HTTP adat` | `No HTTP data received` |

HTTP segédszöveg: `A Loxone adatküldésére várunk...` / `Waiting for data from Loxone...`.

Wi-Fi kiesésekor a külön Wi-Fi hibaoldal elsőbbséget élvez. HTTP módban soha ne jelenjen meg MQTT-brokerre utaló hiba. Egy új érvényes csomag után a dashboard automatikusan visszatér, újraindítás nélkül.

---

# 23. Loxone integráció

A Loxone Config Program blokk Pico C kódja állítja elő a JSON-t.

Használt bemenetek:

| Bemenet | Mező |
|---|---|
| I1 | PowerGrid |
| I2 | PowerBat |
| I3 | PowerPV |
| I4 | SocBat |
| I5 | PowerFull |
| I6 | Temperature |
| I7 | WindSpeed |
| I8 | HasSunRadiation |
| I9 | HasPrecipitation |
| I10 | Status |
| I11 | NightMode |
| I12 | BatteryTemperature |

Kimenet:

- Txt1.

Periódus:

- 5000 ms.

A Loxone program:

- ne maszkolja a hibás 0/1 vagy Status értékeket;
- a firmware validátora döntse el, hogy a csomag jó-e.

A Loxone Pico C sajátossága miatt a JSON `sprintf()` formátum:

- egyetlen string literal legyen;
- ne egymás mellé írt, automatikusan összefűzendő C string darabokból álljon.

## 23.1. HTTP POST minta és bekötés

Minta: `examples/loxone-energy-http-post.picoc`. Cél: Miniserver Gen 2 / 17.x; a bemenetek I1–I12 megfeleltetése változatlan. A script csak a body-t állítja össze, a hálózati küldést a virtuális kimenet végzi.

| Beállítás | Érték |
|---|---|
| Virtuális kimenet címe | `http://<kijelző-IP>` |
| Parancs BE állapotban | `/api/energy` |
| HTTP mód BE esetén | `POST` |
| HTTP header BE parancshoz | `Content-Type: application/json` |
| HTTP body BE parancshoz | `<v>` |
| Alkalmazás digitális kimenetként | Kikapcsolva; Txt1 szöveges bekötés |
| KI parancs | Üres |
| Első ismétlés / ismétlések közötti távolság | `0` / `0` – nincs automatikus újraküldés |
| HTTP válasz mentése | Üres; nincs 5 másodpercenkénti SD-napló |
| Hitelesítés | Nincs |

Öt másodpercenként friss teljes csomag szükséges változatlan mérési értékek mellett is. A mintában a JSON objektum belső whitespace-e felváltva változik: ez új szöveges kimeneti értéket ad új mérési mező vagy mesterségesen változtatott mérés nélkül. A tényleges, 5 másodperces POST-küldést Gen 2 / 17.x hardveren ellenőrizni kell.

A virtuális kimenet nem ismételheti időzítve a legutóbbi body-t, mert az elfedné a Pico C program leállását. Újraküldés helyett a következő friss mintával áll helyre az adatfolyam; régi csomagokat nem kell visszajátszani. Részletes bekötés: `docs/http-input.md`.

---

# 24. Wi-Fi

SSID:

- konfigurációból;
- kötelező.

Password:

- konfigurációból;
- üres lehet.

Újracsatlakozás alapérték:

15 másodperc.

Konfigurálható.

Wi-Fi kieséskor:

- azonnali teljes képernyős hiba;
- felirat: `Nincs Wi-Fi kapcsolat`;
- vizuális/animált Wi-Fi hiba grafika;
- automatikus reconnect;
- helyreálláskor automatikus visszatérés.

Statikus IP:

- opcionálisan konfigurálható.

Ha nincs statikus IP:

- DHCP.

---

# 25. Adatforrás kapcsolatvesztése

Az alábbi brokerkezelés kizárólag MQTT módban aktív. HTTP módban nincs tartós brokerkapcsolat; az elérhetőséget az érvényes POST-minták és a 22. fejezet timeoutja jelzi.

Brokerkapcsolat megszakadásakor:

- külön MQTT hibaképernyő;
- ne keveredjen össze a Wi-Fi hibával;
- reconnect alapérték: 15 másodperc;
- konfigurálható.

A kapcsolat helyreállásakor a rendszer automatikusan folytatja a működést.

---

# 26. Indulási folyamat

Külön boot/status képernyő szükséges.

Lépések:

1. SD-kártya;
2. config betöltés;
3. Wi-Fi;
4. NTP;
5. statisztika és web/OTA inicializálása;
6. kiválasztott adatfogadó: MQTT-kapcsolat vagy HTTP POST végpont regisztrálása;
7. első érvényes adatcsomag.

A HTTP végpont csak a konfiguráció, idő és statisztika sikeres inicializálása után fogadhat mérést. HTTP módban a web/adatfogadó indítási hibája blokkoló hiba; a boot nem várhat MQTT-re.

A dashboard csak a szükséges feltételek után jelenik meg.

---

# 27. Induláskori blokkoló hibák

Ha:

- SD nem elérhető;
- config hiányzik;
- config hibás;

akkor:

- ne használjon automatikusan gyári konfigurációt;
- dedikált piros/fekete hibaképernyő;
- konkrét hibaüzenet;
- maradjon ebben az állapotban újraindításig.

A konfiguráció opcionális mezőinek hiánya viszont nem hiba:

- ilyenkor az alapérték használható.

---

# 28. microSD

## 28.1. Fájlstruktúra

```text
/config.json
/stats/2026-10-04.json
/stats/2026-10-03.json
```

A firmware:

- mai;
- tegnapi

napi statisztikát tartja meg.

Régebbi napok törölhetők.

## 28.2. Config olvasás

A `config.json`:

- kizárólag bootkor kerül beolvasásra;
- runtime nem olvassa újra;
- módosítás után restart szükséges.

## 28.3. Fájlrendszer

Elsődlegesen:

- FAT32.

A firmware aktuális támogatását mindig a használt ESP-IDF/FatFs konfiguráció alapján kell dokumentálni.

---

# 29. Futás közbeni SD-hiba

Ha a kártya runtime közben kiesik vagy írási hiba történik:

- dashboard működjön tovább;
- a kiválasztott MQTT/HTTP adatfeldolgozás folytatódjon;
- statisztika RAM-ban gyűljön tovább, amennyire lehetséges;
- jelenjen meg jól látható piros:
  - `SD-kártya hiba`;
- ne legyen teljes képernyős;
- a firmware periodikusan próbálja helyreállítani az SD kapcsolatot.

Elfogadott recovery periódus:

- 1 perc.

Sikeres helyreálláskor:

- figyelmeztetés eltűnik;
- mentés folytatódik.

Folyamatos SD logolás:

- nincs.

---

# 30. Statisztikai adatgyűjtés

Bejövő minták:

- körülbelül 5 másodpercenként.

Grafikon adatpont:

- 5 perces átlag.

Pontok száma:

- 288 / nap / sorozat.

A raw 5 másodperces minták:

- RAM-ban használhatók számításhoz;
- nem kell mindet külön SD-re írni.

---

# 31. Energia-integrálás

A napi energiát a teljesítmény idő szerinti integrálásával kell számolni.

A minták között:

- trapézszabály.

Ha két érvényes minta között a rés nagyobb, mint:

- 30 másodperc alapérték;

akkor:

- a hiányzó időt nem szabad extrapolálni.

Ez konfigurálható:

`max_integration_gap_seconds`

Import/export és töltés/kisütés esetén:

- ha két minta között előjelet vált a teljesítmény;
- a nullametszést lineárisan meg kell határozni;
- a két energiairányt külön kell integrálni;
- ne oltsák ki egymást.

Napváltásnál:

- az éjfélt átlépő mintaköz pontosan kerüljön kettéosztásra;
- az éjfél előtti energia tegnaphoz;
- az éjfél utáni energia a mai naphoz tartozzon.

---

# 32. Statisztika mentés

A RAM állapot periodikusan SD-re kerül.

Alapérték:

- 60 perc.

Konfigurálható.

A felhasználó elfogadott adatvesztési toleranciája:

- legfeljebb körülbelül 1 óra.

Mentési stratégia:

1. temporary file;
2. flush/fsync;
3. backup/replace;
4. sérült főfájl esetén helyreállítási lehetőség.

Cél:

- egy váratlan áramszünet ne tegye könnyen használhatatlanná a teljes napi statisztikát.

---

# 33. Napi számítások

## 33.1. Termelés

`PowerPV` integrálja.

## 33.2. Fogyasztás

`PowerFull` integrálja.

## 33.3. Hálózati vételezés

`PowerGrid > 0` rész integrálja.

## 33.4. Visszatáplálás

`PowerGrid < 0` abszolút része integrálja.

## 33.5. Akkumulátor töltés

`PowerBat < 0` abszolút része integrálja.

## 33.6. Akkumulátor kisütés

`PowerBat > 0` integrálja.

---

# 34. Önellátás

Ez **napi**, nem pillanatnyi mutató.

Képlet:

`(mai fogyasztás - mai hálózati vételezés) / mai fogyasztás × 100`

Az eredmény:

- minimum 0%;
- maximum 100%.

Tegnaphoz ugyanazon lezárt 5 perces intervallumig hasonlítható.

---

# 35. Megtakarított pénz

Helyben felhasznált PV:

`napi PV termelés - napi hálózati export`

Megtakarítás:

`helyi PV × electricity_purchase_price + grid export × feed_in_price`

Alapérték:

- villamos energia vásárlási ár: 65 Ft/kWh;
- visszatáplálási ár: 5 Ft/kWh.

A pénznem:

- konfigurálható egyszerű szöveg;
- alapérték: `Ft`.

---

# 36. CO2 megtakarítás

Képlet:

`mai teljes PV termelés × co2_factor_kg_per_kwh`

Alapérték:

`0.381 kg CO2e/kWh`

Megjelenítés:

- 1 tizedes kg.

---

# 37. Fa-egyenérték

Képlet:

`megtakarított CO2 kg / tree_absorption_kg_per_year`

Alapérték:

`22 kg CO2/fa/év`

Megjelenítés:

- 2 tizedes.

---

# 38. Idő és NTP

NTP szerver konfigurálható.

Elfogadott alapérték:

`hu.pool.ntp.org`

Időzóna alapérték:

`Europe/Budapest`

`auto_dst` alapérték:

`1`

Ha:

`auto_dst = 0`

akkor:

`utc_offset_minutes`

használandó.

Alapérték:

`60`

---

# 39. NTP indulási működés

A normál dashboard csak sikeres indulási időszinkron után induljon el.

Indulási hiba esetén:

- időszinkron hiba;
- retry.

Alap retry:

15 másodperc.

Konfigurálható.

---

# 40. NTP runtime működés

Normál újraszinkron alapérték:

- 6 óránként.

Ha ez sikertelen:

- dashboard működjön tovább a belső óráról;
- retry alapérték:
  - 15 perc.

Siker után:

- visszaáll a normál 6 órás ciklus.

---

# 41. Nyelv

Konfigurálható:

- `hu`
- `en`

Alapérték:

`hu`

A nyelvérték:

- kis-/nagybetű érzéketlenül elfogadható.

Példák:

- hu
- HU
- Hu

A teljes felhasználói UI legyen lokalizálható:

- dashboard;
- bootképernyő;
- hibaüzenetek;
- weboldal;
- OTA oldal;
- státuszfeliratok.

---

# 42. Konfiguráció

A minta:

`config.sample.json`

minden implementált mezőt és alapértéket tartalmazzon. Az elfogadott bővítés implementálásakor a `DataReceptMode` és `http` mezőkkel is frissítendő.

Gyökérszintű módválasztó: `"DataReceptMode": "mqtt"`. Hiányában `mqtt`; hibás értéknél blokkoló konfigurációs hiba. A módváltás újraindítást igényel.

## 42.1. project

```json
{
  "title": "Otthoni energia",
  "language": "hu",
  "currency": "Ft"
}
```

## 42.2. wifi

```json
{
  "ssid": "",
  "password": "",
  "reconnect_seconds": 15,
  "network": {
    "use_static_ip": 0,
    "ip": "",
    "gateway": "",
    "subnet": "",
    "dns": ""
  }
}
```

SSID:

- kötelező a valódi configban.

Password:

- üres lehet.

Statikus IP:

- opcionális.

## 42.3. time

```json
{
  "ntp_server": "hu.pool.ntp.org",
  "timezone": "Europe/Budapest",
  "auto_dst": 1,
  "utc_offset_minutes": 60,
  "startup_retry_seconds": 15,
  "resync_hours": 6,
  "background_retry_minutes": 15
}
```

## 42.4. mqtt

```json
{
  "broker": "",
  "port": 1883,
  "topic": "loxberry/mqttgateway/energymonitor/state",
  "username": "",
  "password": "",
  "data_timeout_seconds": 30,
  "reconnect_seconds": 15
}
```

Broker:

- kizárólag `DataReceptMode=mqtt` esetén kötelező.

## 42.5. display

```json
{
  "day_brightness": 80,
  "night_brightness": 20
}
```

## 42.6. statistics

```json
{
  "save_interval_minutes": 60,
  "max_integration_gap_seconds": 30
}
```

## 42.7. energy

```json
{
  "electricity_purchase_price": 65,
  "feed_in_price": 5,
  "co2_factor_kg_per_kwh": 0.381,
  "tree_absorption_kg_per_year": 22
}
```

## 42.8. ota

```json
{
  "username": "",
  "password": ""
}
```

Ha mindkettő üres:

- OTA/web nincs hitelesítve.

Ha használjuk:

- mindkettő legyen kitöltve.

## 42.9. http

```json
{
  "data_timeout_seconds": 30
}
```

A `http` objektum opcionális; hiánya esetén is 30 másodperc az alap timeout. Csak HTTP módban érvényes. Nem tartalmaz felhasználónevet, jelszót vagy API-kulcsot. A POST útvonal és a 80-as port fix. MQTT módban a HTTP adatspecifikus konfiguráció nem blokkolhatja az indulást.

Példa a módválasztást érintő részletre (a többi konfigurációs objektum megtartandó):

```json
{
  "DataReceptMode": "http",
  "http": { "data_timeout_seconds": 30 }
}
```

---

# 43. Fix, nem konfigurálható értékek

Hostname:

`zolee-energy-flow-display`

Nem konfigurálható.

Kijelző orientáció:

- fix fekvő.

Grafikon slot:

- fix 5 perc.

MQTT QoS:

- fix 1.

---

# 44. Helyi weboldal

Elérés:

`http://zolee-energy-flow-display.local/`

Mutassa:

- firmware verzió;
- uptime;
- Wi-Fi RSSI;
- IP cím;
- aktív adatfogadási mód (`mqtt` / `http`);
- MQTT módban brokerkapcsolat; HTTP módban a POST-fogadó üzemkészsége;
- utolsó érvényes csomag ideje és azóta eltelt idő, a módnak megfelelő megnevezéssel;
- SD állapot;
- reset ok;
- utolsó fontos események/hibák RAM-ból.

Ne legyen folyamatos SD log.

---

# 45. Webes újraindítás

Az információs oldalról az eszköz újraindítható.

Kell:

- megerősítés;
- ugyanaz a Basic Auth védelem, mint OTA-nál.

---

# 46. OTA

OTA oldal:

`http://zolee-energy-flow-display.local/update`

Felhasználónév/jelszó:

- konfigurálható;
- üres érték esetén nincs auth.

OTA architektúra:

- két firmware partíció;
- automatikus rollback.

---

# 47. OTA érvényesítés

Új firmware sikeresnek tekinthető, ha legfeljebb:

**3 percen belül**

teljesül:

- SD elérhető;
- config betöltés sikeres;
- Wi-Fi csatlakozott;
- NTP sikeres;
- MQTT módban: brokerkapcsolat sikeres;
- HTTP módban: a HTTP szerver és a `/api/energy` POST-fogadó sikeresen elindult.

**Nem feltétel:**

- érvényes MQTT vagy HTTP mérési csomag érkezése.

HTTP módban az MQTT-kapcsolat sem feltétel. A Loxone ideiglenes leállása vagy a mérési adat hiánya önmagában nem okozhat rollbacket. A háromperces érvényesítési határidő és minden más kötelező feltétel megmarad.

Ha a feltételek nem teljesülnek:

- automatikus rollback az előző firmware-re.

---

# 48. Hibaoldalak

Minden fő hibatípus:

- egyértelmű grafika;
- lehetőleg finom animáció;
- jól olvasható főszöveg;
- rövidebb segédszöveg.

Különálló hibák:

- Wi-Fi;
- MQTT kapcsolat;
- MQTT adat-timeout;
- HTTP adat-timeout (`Nem érkezik HTTP adat`);
- első adatra várakozás a kiválasztott mód szerint;
- SD/config fatal boot hiba.

A végleges hibaikonok is asset-alapú, minőségi grafikák legyenek.

---

# 49. Watchdog és megbízhatóság

A firmware hosszú távú 24/7 működésre készül.

Elvárás:

- task watchdog;
- reset ok naplózása;
- memória-szivárgások kerülése;
- LVGL objektumok kontrollált életciklusa;
- SD írás gyakoriságának minimalizálása;
- network reconnect;
- OTA rollback;
- hibás MQTT/HTTP csomagok izolálása.

---

# 50. Touch használat

A kijelző érintőképes, de v1-ben:

- touch funkció nincs;
- nincs gomb;
- nincs navigáció;
- nincs beállítás a kijelzőről.

Minden konfiguráció:

- SD `config.json`;
- vagy helyi OTA/web funkció.

---

# 51. Nem célok a v1-ben

Nem része a specifikációnak:

- heti/havi/éves historikus UI;
- érintős menürendszer;
- helyi beállításszerkesztő;
- cloud backend;
- közvetlen inverter API;
- a Loxone HTTP API-jának lekérdezése a kijelző felől (a Loxone → kijelző HTTP POST küldés viszont része a specifikációnak);
- felhőalapú távoli OTA;
- statisztika kézi webes törlőgombja;
- több dashboard oldal.

---

# 52. Build és CI

Minden main push és pull request esetén:

- ESP-IDF 5.5.4 build;
- target: esp32p4.

Sikeres build artifactként legalább:

- app bin;
- bootloader bin;
- partition table bin;
- flasher args.

A CI buildnek zöldnek kell lennie a hardveres tesztelés előtt.

## 52.0. Automatikus release a main ágra történő PR-merge után

- A kiadás a PR tényleges main-merge-je után indul; review-jóváhagyás vagy merge nélküli lezárás önmagában nem elég.
- A patch verzió automatikusan nő: `v1.2.3 → v1.2.4`. A legnagyobb stabil verziótag/release az alap; prerelease és CI-verzió kimarad. A régi `Prod` taghez tartozó `0.0.1` release-cím is verzióalap, tehát ebből `v0.0.2` következik. Verzióelőzmény nélkül `v0.0.1` az első.
- Mindig a konkrét PR merge SHA-ja fordul, az új verzióval. Minden teszt és a build sikeres befejezése előfeltétel.
- A kiadás csak az OTA/USB csomag és az ellenőrző fájlok feltöltése után publikálódik; addig draft. Sikertelen teszt/build nem hoz létre taget vagy release-t.
- A kiadások közös, megszakítás nélküli sorban futnak (`queue: max`, maximum 100 várakozó). Ugyanaz a már kiadott merge újrafuttatva nem növeli újra a verziót.
- A sikeres build után kizárólag a publikáló job készíthet annotált verziótaget, amely a PR/merge SHA azonosítót tartalmazza. Ez az előkészítő számára olvasási joggal is felismerhető folytatási pont; a draft release-ek listázásához nem kap külön írási jogot.
- A feltöltés újrafuttatható, azonos tartalom kimarad; más commitra mutató tag és eltérő asset nem írható felül.
- A workflow közvetlenül hívja az újrahasznosított firmware-buildet és publikálást, mert GITHUB_TOKEN-nel létrehozott release nem indít új release-eseményes workflow-t. Nem igényel személyes tokent.
- Kézi release/prerelease továbbra is támogatott. A main ágba közvetlen push csak CI-ellenőrzés, nem automatikus kiadás.

## 52.1. Automatikus GitHub Release csomagolás

A felhasználó a GitHubon létrehozza a taget/release-t, majd **Publish release** művelettel publikálja. A `release: types: [published]` esemény indítja a fordítást normál release és pre-release esetén egyaránt. Draft mentése nem indít publikálást; puszta release-leírás szerkesztése nem fordít újra.

A release tagjéhez tartozó pontos commitot kell fordítani ESP-IDF 5.5.4 alatt, ESP32-P4 célra, nem a közben elmozduló `main` ágat. A firmware fájlok a tesztek és build sikere után jelennek meg ugyanannak a release-nek az Assets részében. A publikálás és a csatolmányok megjelenése között a CI futási ideje telik el.

## 52.2. Verzióazonosság

- Tagformátum: `vMAJOR.MINOR.PATCH`, opcionális előzetes utótaggal, pl. `v1.2.0-rc.1`.
- A teljes tag legyen a beégetett `PROJECT_VER`, a weboldalon kijelzett firmware-verzió és a fájlnevek verziórésze.
- Az ESP-IDF verziómező hosszkorlátját előre ellenőrizni kell; a kiadási tag legfeljebb 31 ASCII karakter, nem csonkolható.
- A release build ne használja a kódban maradt `0.1.0` alapverziót.
- Mellékelni kell a tag, commit SHA, célhardver, ESP-IDF verzió és csomagformátum azonosítóit egy `build-info.json` fájlban.

## 52.3. Release-csatolmányok

| Fájl (példa) | Rendeltetés |
|---|---|
| `ZoleeEnergyFlowDisplay-v1.2.0-ota.bin` | A build alkalmazás-binárisa, közvetlenül feltölthető a `/update` oldalra |
| `ZoleeEnergyFlowDisplay-v1.2.0-usb.zip` | Teljes első USB-telepítéshez szükséges csomag |
| `SHA256SUMS.txt` | Az OTA bináris és az USB ZIP SHA-256 ellenőrzőösszege |
| `build-info.json` | Verzió és eredet visszakövetése |

Az OTA-fájl nyers alkalmazás-image; nem ZIP, bootloader, partíciós tábla vagy összevont teljes flash-image. A készülék továbbra is kézi fájlfeltöltéssel frissül; GitHub-belépés vagy automatikus GitHub-letöltés nem szükséges az eszközön.

## 52.4. USB-s első telepítőcsomag

A ZIP tartalmazza ugyanabból a buildből az alkalmazást, bootloadert, partíciós táblát és a flasher manifest által hivatkozott minden további image-et (pl. OTA-init adat, ha szükséges), valamint a hozzájuk tartozó offseteket és flash paramétereket. A csomagon belüli összes hivatkozás feloldható legyen, CI-s abszolút elérési utak nélkül.

Kötelező melléklet egy magyar telepítési útmutató: Windows COM-port kiválasztás, USB boot/download mód, szükséges esptool telepítése/rögzített verziója, pontos parancs a ZIP gyökeréből, első indítás és SD-konfiguráció. A flash parancs címeit/paramétereit a build manifestből kell előállítani, nem kézzel másolni egy másik buildből. A konfigurációt és az SD-statisztikát nem szabad a firmware-csomagba beégetni.

Az USB ZIP első telepítéshez/helyreállításhoz való; a szokásos frissítés az OTA `.bin` feltöltése. Az alkalmazás tartalma a két csomagban bájtra egyezzen.

## 52.5. Hibák és ismételt futtatás

- Hibás build, teszt, verzió vagy hiányzó USB-image esetén nincs használhatónak jelölt firmware-csomag.
- A workflow legyen kézzel újrafuttatható ugyanarra a release-re; más taget/release-t nem módosíthat.
- Ismételt feltöltésnél az azonos nevű és checksumú fájl megtartható; eltérő tartalmú létező release-assetet nem szabad csendben felülírni.
- A `GITHUB_TOKEN` csak a szükséges jobban kapjon `contents: write` jogosultságot; külön személyes token ne legyen követelmény.
- Release-futást egy új main push vagy másik release ne szakíthasson meg.
- GitHub immutable release esetén a publikálás utáni csatolás tiltott lehet. Ezt az implementáció során ellenőrizni kell; ilyen ütközésnél egyértelmű hiba szükséges, a repository védelmeit nem szabad automatikusan kikapcsolni.

---

# 53. Kötelező példafájlok

Repository-ban legyen:

`examples/mqtt-payload.sample.json`

és:

`examples/loxone-energy-payload.picoc`

További kötelező minta:

`examples/loxone-energy-http-post.picoc`

HTTP bekötési útmutató: `docs/http-input.md`.

Mindkét Loxone példa a közös payload formátumot használja. A HTTP-minta a POST body-t állítja elő, nem közvetlen socket-/HTTP-kliens a Pico C programon belül.

---

# 54. Elfogadási tesztek

A projekt nem tekinthető késznek pusztán attól, hogy lefordul.

Kötelező hardveres validáció:

- USB flash valódi Waveshare 10.1 panelre;
- 1280×800 tájolás;
- háttérvilágítás;
- PSRAM;
- FAT32 SD;
- config betöltés;
- statisztikai fájl írás;
- reboot utáni statisztika visszatöltés;
- Loxone → MQTT → kijelző teljes lánc;
- 5 másodperces valós adat;
- Wi-Fi kiesés;
- Wi-Fi visszatérés;
- MQTT kiesés;
- MQTT reconnect;
- MQTT adat-timeout;
- hibás JSON;
- részleges JSON;
- tartományon kívüli mező;
- SD eltávolítás;
- SD helyreállítás;
- Status 1;
- Status 2;
- Status 3;
- Status 4;
- nappali mód;
- éjszakai mód;
- OTA update;
- hibás OTA / rollback;
- napváltás;
- legalább egy hosszabb, lehetőleg teljes napos folyamatos futás.

## 54.1. HTTP mód elfogadása

- Hiányzó `DataReceptMode` mellett változatlan MQTT működés; ismeretlen mód blokkoló config hiba.
- HTTP módban MQTT-config és broker nélkül indulás, MQTT-kliens elindítása nélkül.
- Miniserver Gen 2 / 17.x → POST → kijelző lánc; minden mező, előjel és mértékegység megfelel.
- Legalább 60 másodperc teljesen változatlan mérési értékek mellett is 5 másodpercenként új POST érkezik.
- Pico C program leállítása után nem indul újraküldési ciklus: 30 másodperc elteltével HTTP adat-timeout.
- Első csomag hiánya, hibás/hiányos/duplikált mezős JSON, hibás tartomány, nem JSON tartalom, GET, túlméretes és megszakított body nem módosít élő/statisztikai állapotot.
- Új érvényes csomag automatikusan visszaállítja a dashboardot; Wi-Fi kiesés és visszatérés mindkét módban helyes.
- HTTP adatfogadás bekapcsolt OTA Basic Auth mellett is hitelesítés nélkül működik; az OTA/web oldalak védelme megmarad.
- HTTP módban OTA elfogadása mérési adat és MQTT-broker nélkül; a POST-fogadó indítási hibája viszont nem teljesíti az OTA feltételt.
- HTTP-minták ugyanazt az integrálási, napváltási és SD-helyreállítási regressziós ellenőrzést teljesítik, mint MQTT-nél.

## 54.2. Release és telepítő elfogadása

- Normál és pre-release publikáláskor is a megfelelő tagből készül build; draft mentésekor nincs csomagolás.
- A tag, beégetett verzió, csomagnevek és `build-info.json` összhangban vannak.
- Az Assets részben OTA bináris, teljes USB ZIP, checksum és buildinformáció található; az ellenőrzőösszegek helyesek.
- Az USB manifest összes fájlja szerepel a ZIP-ben; az OTA image azonos az USB-csomag alkalmazásával.
- A release OTA `.bin` a meglévő `/update` oldalon ténylegesen feltölthető, újraindul és a kiválasztott mód szerint validálódik.
- Üres/helyreállítandó Waveshare eszköz USB-s első telepítése a csomagolt magyar útmutató szerint sikeres.
- Hibás build/teszt és ismételt workflow-futás nem eredményez másik release-hez tartozó vagy csendben felülírt binárist.

---

# 55. Vizuális elfogadási teszt

A végleges UI-t a tényleges kijelzőn kell ellenőrizni.

Eljárás:

1. firmware felprogramozása;
2. valós/szimulált MQTT és HTTP adatok küldése a kiválasztott módban;
3. nappali dashboard fotó;
4. éjszakai dashboard fotó;
5. összevetés a jóváhagyott eredeti látványtervvel;
6. ikonméret, pozíció, padding, font, árnyék, grafikon és szín finomhangolása;
7. ismételt hardverfotó.

Vizuálisan akkor tekinthető késznek, ha:

- első ránézésre ugyanazt a vizuális rendszert adja;
- az ikonok minősége nem primitív;
- az arányok és whitespace-ek megfelelnek;
- a grafikonok és központi energiafolyam ugyanazt a vizuális súlyt kapják;
- a layout nem „fejlesztői prototípusnak”, hanem kész terméknek tűnik.

---

# 56. Implementációs prioritás a specifikáció alapján

A hátralévő munka prioritása:

1. build stabilitás;
2. az elkészült, 29 ikonos PNG-készlet hardveres vizuális ellenőrzése;
3. dashboard pixel- és layoutfinomítás;
4. második grafikon nullavonalra töltött gradient fill;
5. az új státusz- és hibaikonok ellenőrzése a tényleges üzemállapotokban;
6. HU/EN végső audit;
7. parser/statisztika automatizált tesztek;
8. hardveres első flash;
9. valós Loxone/MQTT teszt;
10. SD/OTA/reconnect tesztek;
11. hosszú futás;
12. vizuális fotó alapján utolsó finomítás.

---

# 57. Végleges minőségi elv

A projekt két egyenrangú célt teljesít:

1. **megbízható, helyesen számoló, 24/7 firmware**, és
2. **a jóváhagyott látványtervhez hű, prémium minőségű vizuális megjelenés**.

Egy funkcionálisan helyes, de vizuálisan primitív felület nem tekinthető késznek.

Ugyanígy egy szép UI sem tekinthető késznek, ha:

- hibásan számol;
- adatot veszít;
- nem kezeli a kapcsolatvesztést;
- nem működik OTA rollback;
- vagy hosszú távon instabil.

A kész termék mindkét követelményt egyszerre teljesíti.



# 58. A 2026-10-05-i bővítés forrásai

- [GitHub Actions release események](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#release): `published`, normál és pre-release.
- [GitHub immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases): publikált csatolmányok módosíthatósága.
- [Loxone virtuális be- és kimenetek](https://www.loxone.com/enen/kb/virtual-inputs-outputs/): HTTP virtuális kimenetek.
- [FlexumShare gyártói integráció a Loxone Libraryben, 7. oldal](https://api.library.loxone.com/downloader/file/2733/Instruction%20Live-Data_FlexumShare%20platform_V1.1.pdf): POST, JSON header, `<v>` body beállítás. A jelen projekt adatfogadása a felhasználó döntése szerint nem kér API-kulcsot.
- Felhasználói döntések: 2026-10-05; Miniserver Gen 2 / 17.x, kizárólag POST, hitelesítés nélkül, 5 másodperces küldés, 30 másodperces timeout, OTA-nál nem feltétel mérési csomag.

## Asztali UI-szimulátor és demó adatforrás

- Windows x64 / Visual Studio Professional 2026, CMake Debug konfiguráció és F5 indítás, fizikai kijelző nélkül.
- A firmware tényleges LVGL UI-ja, grafikonjai, ikon- és fontállományai futnak; külön rajzolt UI-másolat nem elfogadható.
- Induláskor egy teljes demó JSON-csomag, majd 5 másodperces ciklus; közös ingest, payload-validáció és állapottárolás használata.
- Demó forgatókönyvek: hálózati import/export, akkumulátortöltés/merítés, nappal/éjszaka, eső/hó, státuszok és akkumulátor-hőmérsékleti színváltások. Valódi UI-animáció és irány/szín logika.
- Előre feltöltött, egyértelműen demó grafikonok és tegnapi összehasonlítás; memóriában frissülő statisztika, valódi SD-adatok módosítása nélkül.
- Adatküldés szüneteltethető; 30 másodperc után HTTP-adathiány képernyő, érvényes új csomagnál automatikus helyreállás.
- A demó külön asztali buildcél, firmware-ben soha nem kapcsol be automatikusan a Debug beállítástól.
- Windows MSVC CI-fordítás és headless állapot/render regresszió. Részletes használat: `docs/simulator.md`.

### UI-finomítások: villogásmentes asztali megjelenítés, függőleges SOC

- A szimulátor frissítés közben nem törölheti feketére a látható ablakot; teljes képet memóriában összeállítva kell megjelenítenie, átméretezéskor is megtartva a képarányt.
- Az akkumulátor SOC-sávja az ikon bal oldalába illesztve, a körön belül függőleges, alulról felfelé töltődik; 0–100% tartomány és a meglévő SOC-színküszöbök maradnak. Ez a firmware és az asztali szimulátor közös UI-eleme.

- Pontosított SOC-elhelyezés a felhasználó piros jelölése alapján: a kör tartalmához képest x=24, y=23, 4×46 pixel; a sáv az akkumulátor testén belül, a villámtól balra helyezkedik el.

### Önálló napi akkumulátorkártya

- Az energiaáramlás alatt külön, azonos szélességű (490 px) kártya: „Akku infó – ma” / „Battery info – today”, akkumulátor + információjel kompozit ikonnal. A kártyák között 12 px térköz.
- Két egyenlő oszlopban a napi összesített töltés és kisütés (kWh), nagyobb értékfelirattal és a meglévő tegnapi összehasonlítással. A SOC, pillanatnyi teljesítmény és hőmérséklet csak az áramlásábrán marad.
- SOC-sáv: 20% alatt piros; 20–50% között, a határokat beleértve narancssárga; 50% felett zöld. Az éjszakai árnyalat ezeknek a színeknek a tompított változata.

### Véglegesített kompakt akkukártya és szimmetrikus áramlásábra

- Az akkukártya 490×64 px, bal oldalon nagyobb akkumulátor/információ ikonnal, mellette két adatoszloppal. Külön kártyacím nincs. Feliratok: „Össz. töltés ma”, „Össz. leadás ma” (EN: „Charged today”, „Discharged today”). Napi kWh és tegnapi összehasonlítás megmarad.
- Energiaáramlás-kártya: 490×492 px. A négy összekötő szakasz egyformán 64 px hosszú; a hub és a négy energiacsomópont középpontjának távolsága 145 px. A kártyák között 12 px térköz. Ezek a méretek és feliratok felülírják a korábbi, magasabb akkukártya elrendezését.

### Összehasonlítások, irányikonok és függőleges igazítás

- A napi akkukártyán mindkét érték jobb oldalán jelenik meg az előző nap azonos lezárt időszakához tartozó százalékos összehasonlítás. Hiányzó vagy nulla tegnapi viszonyítási alap esetén „—” jelzés, nem üres hely.
- Külön PNG ikon a hálózati vételezéshez (narancssárga nyíl: hálózat → ház) és visszatápláláshoz (zöld nyíl: ház → hálózat).
- A bal oldali négy csempe ikonja és teljes szövegblokkja a saját kártyamagassághoz viszonyítva függőlegesen középre igazított.
- A fejléc dátuma, időjárásikonja és hőmérséklete, szélikonja és szélsebessége, valamint órája közös függőleges középvonalon helyezkedik el.
- Az alsó teljes szélességű összefoglaló függőleges elválasztó vonalai megszűnnek; a mezők pozíciói változatlanok.

### Státusz, összefoglaló és szélriasztás finomítása

- Az alsó összefoglaló szövegblokkjai függőlegesen középre igazítottak; az összes ikon, a státuszt is beleértve, azonos középvonalon, 54 px körháttéren jelenik meg.
- A státuszmezőnek egyik állapotban sincs kerete. Az ikon halvány körháttere normál esetben zöld, áramszünetre felkészüléskor narancs, szigetüzemben kék, hiba esetén piros; éjszaka tompított árnyalatokkal.
- Bal oldali ikonhátterek: termelés és visszatáplálás halvány zöld, fogyasztás halvány citromsárga, hálózati vételezés halvány narancs.
- A termelés/fogyasztás grafikon jelmagyarázatában a teljes „Fogyasztás” felirat szerepel.
- A fejléc szélsebessége 35 km/h felett félkövér narancssárga, 50 km/h felett félkövér piros. Pontosan 35-nél normál, pontosan 50-nél narancssárga; csökkenéskor az alapstílus visszaáll. A küszöbvizsgálat a kerekítetlen beérkező értékkel történik.
