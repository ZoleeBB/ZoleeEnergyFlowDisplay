# Zolee Energy Flow Display – részletes fejlesztési specifikáció

> **Státusz:** normatív fejlesztési specifikáció  
> **Célhardver:** Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1  
> **Célfelbontás:** 1280×800, fix fekvő tájolás  
> **Firmware alap:** ESP-IDF 5.5.4 + LVGL  
> **Elsődleges nyelv:** magyar  
> **Utolsó összefoglaló frissítés:** 2026-10-05

Ez a dokumentum a projekt során végigbeszélt igényeket, elfogadott döntéseket, viselkedési szabályokat, konfigurációs alapértékeket, UI-elvárásokat, MQTT/Loxone integrációt, statisztikai számításokat, hibakezelést, OTA működést és vizuális követelményeket egyetlen specifikációban foglalja össze.

Ha a régebbi dokumentáció vagy egy korábbi implementáció ettől eltér, **ez a dokumentum tekintendő a fejlesztési cél elsődleges leírásának**, kivéve, ha egy későbbi, kifejezett felhasználói döntés módosítja.

---

# 1. Alap igény

A cél egy falra vagy állandó helyre szerelt, 24/7 üzemelő, önálló otthoni energiamonitor létrehozása, amely:

- valós időben megjeleníti a ház energiaáramlását;
- vizuálisan a jóváhagyott eredeti látványtervet követi;
- Loxone rendszerből MQTT-n kapja az adatokat;
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

Az MQTT `Status` mező:

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

# 18. MQTT adatforrás

A firmware kizárólag MQTT-ről kap energiamérési adatokat.

Nincs közvetlen:

- Loxone HTTP;
- Loxone WebSocket;
- Modbus;
- inverter API.

## 18.1. Kapcsolat

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

## 18.2. Publikálási periódus

Loxone oldalról:

- egy teljes JSON csomag;
- 5 másodpercenként.

Ajánlott:

- retained message.

A retained üzenet korát a firmware **nem** próbálja ellenőrizni.

Nincs payload timestamp mező.

---

# 19. MQTT payload

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

# 20. MQTT validáció

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

# 21. MQTT mezőtartományok

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

# 22. MQTT adat-timeout

Alapérték:

30 másodperc.

Konfigurálható.

A timeout az:

**utolsó érvényes MQTT csomagtól**

számít.

Hibás csomag:

- nem nullázza a timeoutot.

Ha nincs érvényes adat:

- teljes képernyős hiba.

Felirat magyarul:

`Nem érkezett MQTT adatcsomag.`

Kisebb segédszöveg:

`Kapcsolat helyreállítására várunk...`

Érvényes csomag visszatérésekor:

- dashboard automatikusan visszaáll.

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

# 25. MQTT kapcsolatvesztés

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
5. MQTT;
6. első érvényes adatcsomag.

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
- MQTT feldolgozás folytatódjon;
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

minden támogatott mezőt és alapértéket tartalmazzon.

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

- valódi configban kötelező.

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
- MQTT kapcsolat;
- utolsó érvényes MQTT csomag ideje;
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
- MQTT broker kapcsolat sikeres.

**Nem feltétel:**

- érvényes MQTT payload érkezése.

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
- első adatra várakozás;
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
- hibás MQTT csomagok izolálása.

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
- közvetlen Loxone HTTP API;
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

---

# 53. Kötelező példafájlok

Repository-ban legyen:

`examples/mqtt-payload.sample.json`

és:

`examples/loxone-energy-payload.picoc`

A Loxone példa legyen használható, dokumentált és a firmware payload formátumával azonos.

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

---

# 55. Vizuális elfogadási teszt

A végleges UI-t a tényleges kijelzőn kell ellenőrizni.

Eljárás:

1. firmware felprogramozása;
2. valós/szimulált MQTT adatok küldése;
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
2. assetek teljes PNG minőségűre cserélése;
3. dashboard pixel- és layoutfinomítás;
4. második grafikon nullavonalra töltött gradient fill;
5. összes státusz- és hibaikon végleges assetre cserélése;
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

