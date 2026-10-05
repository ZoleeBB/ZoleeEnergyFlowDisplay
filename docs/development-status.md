## 2026-10-05 – Teljes újraalkotott PNG-készlet

- 29/29 futásidejű ikon PNG: 14 elfogadott grafika és 15 új időjárás-/állapotikon.
- A régi kivágásos PNG-k lecserélve, minden beágyazott SVG-ikon kivezetve.
- Az eredeti nagyfelbontású generált képek és a reprodukálható méretezés a repóban.
- Külön natív körvonal mind a négy energia-node körül; színe a kapcsolattal azonos.
- Az akkumulátor körvonala energiairány szerinti; a SOC külön sávot vezérel.
- A beágyazott PNG-méreteket CMake méri, nincs kézzel rögzített bájtszám.
- Az ESP-IDF 5.5 fájlnév-alapú beágyazási szimbólumai javítják a korábbi linkerhibát.
- A hardveren történő végső megjelenítési ellenőrzés továbbra is szükséges.

Az alábbi korábbi bejegyzések történeti állapotot írnak le.

# Fejlesztési állapot és hátralévő feladatok

Ez a lista a teljes fejlesztési egyeztetés, a jóváhagyott eredeti látványterv és a repository aktuális állapota alapján készült.

## Azonnali prioritás

- [x] MQTT parser buildhibájának javítása a whitespace escape karakterek helyreállításával.
- [x] Az elfogadott alapértelmezett NTP szerver visszaállítása: `hu.pool.ntp.org`.
- [x] Az indulás utáni blokkoló hibaüzenetek HU/EN lokalizálásának javítása.
- [x] Grid import/export és akkumulátor töltés/kisütés pontos integrálása akkor is, ha két minta között előjelet vált a teljesítmény.
- [x] Futás közbeni SD-csere után a `/stats` könyvtár automatikus újralétrehozása.
- [x] Tegnapi összehasonlítás elrejtése, ha a megfelelő lezárt időszakban valójában nincs tegnapi mérési adat.
- [x] A dashboard makro-elrendezésének visszaigazítása az eredeti jóváhagyott látványtervhez.
- [x] A fő energiaáramlás elrendezésének közelítése az eredeti látványhoz: kör alakú elemek, központi hub, külön ház-áramlási ág.
- [x] A napi kártyák eredeti stílusának visszaállítása: nagy ikon, cím, „Ma összesen”, nagy érték és tegnapi összehasonlítás.
- [x] A két grafikon jobb oldali, egymás alatti elrendezése az eredeti vizuális hierarchiának megfelelően.
- [x] Grafikon Y-tengely értékek, finom rács, aktuális idő jelölése és a második grafikon nullavonalának kialakítása.
- [x] Az alsó összefoglaló sáv visszaállítása a megtakarítás / CO2 / fa-egyenérték / önellátás / státusz adatokhoz.
- [x] A fejléc eredeti vizuális karakterének visszaállítása navigációs gombok nélkül: címikon, projektcím, alcím, dátum, időjárás, szél és idő.
- [x] Vektoros assetkészlet bevezetése a felismerhető ikonokhoz; a dashboard, időjárás, státusz és hibaoldalak már nem LVGL karakterikonokra vagy kézzel összerakott ikon-primitívekre épülnek.
- [ ] Második, előjeles grafikon finom áttetsző kitöltésének megvalósítása a nullavonal irányába.
- [ ] Az egyszerű LVGL szimbólumok további közelítése az eredeti látványterv részletes napelem / hálózat / akkumulátor / ház ikonvilágához.
- [ ] Pixel-, betűméret- és térközfinomítás valódi kijelzőfotó alapján.

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
- [x] A konfigurálható NTP újraszinkron periódus ténylegesen felülírja az lwIP gyári 1 órás periódusát.
- [x] A config.json teljes tartalmának szigorú JSON-validálása, trailing szemét elutasításával.
- [x] Maximális hosszúságú OTA Basic Auth felhasználónév/jelszó támogatása.
- [x] Akkumulátor SOC sáv visszaállítása a kör alakú akkumulátor-grafikába.
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

A dashboard makro-elrendezése már az eredeti jóváhagyott látványtervet követi: lekerekített fejléc, bal oldali nagy napi kártyák, központi kör alakú energiaáramlás, jobb oldali egymás alatti grafikonok és alsó összefoglaló szalag.

Szándékos eltérés az eredeti képtől, hogy a későbbi specifikáció szerint nincs navigációs tabsor vagy dátumválasztó, viszont külön hálózati vételezés és visszatáplálás, részletes akkumulátoradatok és két kombinált grafikon szerepel.

A vizuális egyezés még nem tekinthető pixelpontosan validáltnak. Ehhez a firmware-t a tényleges 1280x800-as Waveshare panelen kell lefuttatni, a kijelzőről fotót készíteni, majd az eredeti referenciával összevetni. A hátralévő vizuális munka főként az ikonok részletessége, az előjeles grafikon kitöltése, valamint a fontok, térközök és méretek finomhangolása.

