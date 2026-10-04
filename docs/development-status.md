# Fejlesztési állapot és hátralévő feladatok

Ez a lista a teljes fejlesztési egyeztetés, a jóváhagyott eredeti látványterv és a repository aktuális állapota alapján készült.

## Azonnali prioritás

- [x] MQTT parser buildhibájának javítása a whitespace escape karakterek helyreállításával.
- [x] Az elfogadott alapértelmezett NTP szerver visszaállítása: `hu.pool.ntp.org`.
- [x] Az indulás utáni blokkoló hibaüzenetek HU/EN lokalizálásának javítása.
- [x] Grid import/export és akkumulátor töltés/kisütés pontos integrálása akkor is, ha két minta között előjelet vált a teljesítmény.
- [x] Futás közbeni SD-csere után a `/stats` könyvtár automatikus újralétrehozása.
- [x] Tegnapi összehasonlítás elrejtése, ha a megfelelő lezárt időszakban valójában nincs tegnapi mérési adat.
- [ ] A dashboard vizuális szerkezetének visszaigazítása az eredeti jóváhagyott látványtervhez.
- [ ] A fő energiaáramlás elrendezésének és megjelenésének közelítése az eredeti látványhoz: kör alakú elemek, központi hub, külön ház-áramlási ág.
- [ ] A napi kártyák eredeti stílusának visszaállítása: nagy ikon, cím, „Ma összesen”, nagy érték és tegnapi összehasonlítás.
- [ ] A két grafikon jobb oldali, egymás alatti elrendezése az eredeti vizuális hierarchiának megfelelően.
- [ ] Grafikon Y-tengely értékek, finom rács, aktuális idő jelölése és a második grafikon nullavonalának vizuális finomítása.
- [ ] Az alsó összefoglaló sáv visszaállítása a megtakarítás / CO2 / fa-egyenérték / önellátás / státusz adatokhoz.
- [ ] A fejléc eredeti vizuális karakterének visszaállítása navigációs gombok nélkül: címikon, projektcím, alcím, dátum, időjárás, szél és idő.

## Funkcionális ellenőrzések

- [x] ESP-IDF 5.5.4 célverzió rögzítve.
- [x] Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1, fix 1280x800 fekvő tájolás.
- [x] MQTT TCP/1883, QoS 1, konfigurálható broker és topic.
- [x] 12 kötelező MQTT mező, kis-/nagybetű érzéketlen mezőnevek.
- [x] Teljes csomag elutasítása hiányzó, rossz típusú vagy tartományon kívüli mező esetén.
- [x] 30 másodperces alap MQTT adat-timeout.
- [x] Wi-Fi és MQTT külön teljes képernyős hibaállapot.
- [x] Futás közbeni SD-hiba nem állítja le a dashboardot.
- [x] Loxone Pico C JSON előállító minta elkészült és Loxone alatt működik.
- [x] 5 perces napi grafikonpontok, 288 slot/nap.
- [x] Óránkénti alapértelmezett statisztikamentés.
- [x] Mai + tegnapi nap tárolása.
- [x] Ugyanazon lezárt 5 perces időpontig történő tegnapi összehasonlítás.
- [x] Két OTA alkalmazáspartíció és automatikus rollback.
- [x] OTA elfogadásához nem szükséges érvényes MQTT payload.
- [x] Helyi információs és OTA weboldal.
- [x] A statisztika webes kézi törlése szándékosan nincs megvalósítva.
- [ ] Minden felhasználónak látható webes és diagnosztikai szöveg teljes HU/EN lokalizációjának végső átvizsgálása.
- [ ] Időzóna mező viselkedésének végleges dokumentálása nem Budapest érték esetére.
- [ ] SD-kártya futás közbeni fizikai eltávolításának gyorsabb, írás nélküli felismerése.
- [ ] Automatizált tesztek az MQTT parserhez és a statisztikai integráláshoz.

## Hardveren elvégzendő végső validáció

- [ ] Első teljes USB flash valódi Waveshare 10.1 hardverre.
- [ ] Kijelző tájolás, háttérvilágítás, PSRAM és tényleges LVGL memóriahasználat ellenőrzése.
- [ ] FAT32 SD betöltés és statisztikai írás/újraindítás tesztje.
- [ ] Loxone -> MQTT broker -> kijelző teljes lánc 5 másodperces valós adatokkal.
- [ ] Wi-Fi kiesés, MQTT kiesés, adat-timeout és SD-kártya eltávolítás/helyreállítás tesztje.
- [ ] Mind a négy rendszerállapot vizuális ellenőrzése.
- [ ] Nappali/éjszakai mód és fényerő tesztje.
- [ ] OTA frissítés és szándékosan hibás firmware rollback próbája.
- [ ] Legalább egy teljes napos folyamatos próba, napváltással és statisztikamentéssel.
- [ ] Fizikai kijelzőfotó összevetése a jóváhagyott látványtervvel és az utolsó pixel-/méretfinomítások elvégzése.

## Vizuális audit

Az aktuális UI funkcionálisan már tartalmazza a szükséges adatokat, de még nem tekinthető az eredeti jóváhagyott látványterv pontos adaptációjának.

A legnagyobb eltérések:

1. A napi kártyák jelenleg kis, egységes rácsban vannak, míg az eredeti terv nagy, ikonközpontú kártyákat használ.
2. A fő energiaáramlás jelenleg bal oldali panel, az eredeti tervben ez a központi vizuális elem.
3. A jelenlegi energiaelemek téglalap kártyák; az eredeti terv kör alakú energiaikonokat és központi csomópontot használ.
4. A házhoz nincs külön animált energiaág; ezt az eredeti struktúrához igazítva pótolni kell.
5. A grafikonok jelenleg alul, egymás mellett vannak; az eredeti tervben jobb oldalon, egymás alatt.
6. Az alsó összefoglaló szalag jelenleg hiányzik.
7. A fejléc már helyesen nem tartalmaz navigációt, de vizuálisan még egyszerűbb az eredeti tervnél.

A további UI-fejlesztésnél az eredeti vizuális hierarchiát tartjuk meg, miközben a később elfogadott funkcionális bővítések — külön grid import/export, két kombinált grafikon, részletes akkumulátorkártya — is megmaradnak.
