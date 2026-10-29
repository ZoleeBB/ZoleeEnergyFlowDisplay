# Elektromos otthon – fejlesztési terv

## Elfogadott működés és megjelenés

- Egy új, nappali/éjszakai energiaáramlási téma, az elfogadott kertesházas képpárral.
- Kizárólag közvetlen Loxone módban választható, töltő konfigurálása nélkül is.
- A ház értéke továbbra is a teljes `PowerFull`, az autótöltés ebből nem vonódik le.
- Az `ev-charger` (Elektromos autótöltő) ikonú fogyasztók összegzett, átváltás utáni kW értéke az autó teljesítménye.
- Nincs ilyen fogyasztó: nincs autós felirat és nincs rajzolt autós energiaút; a háttér autója és fekete kábele megmarad.
- Érvényes nulla mérés: 0 W, megálló animáció. Hiányzó mérés nem válhat téves nulla fogyasztássá.
- Egyirányú energiaút a fali töltőtől az autóig, a fekete kábel sima ívét követve.
- A fő energiautak az inverterhez futnak be; az akkumulátor a bal oldali betonozott részen áll.
- A nappali kültéri világítás kikapcsolva, az éjszakai világítás bekapcsolva; piros/bordó kerti növények.
- Web: a fogyasztó- és Otthon-kártyák minden mezője látható. A meglévő színek, világos szövegek, zöld legendák és feliratok megmaradnak.
- Tömör belső mezőrács, balra hangsúlyosabb ikon; magyar név szerinti ékezethelyes ABC-ikonlista.

## Feladatok

- [x] Korlátos autótöltő-összesítés, érvényesség és kapcsolatkezelés, célzott C-tesztek.
- [x] Témaforrás, láthatóság és Loxone-módszűrés, indítás és webes aktiválás ellenőrzése.
- [x] Új téma és natív képek, valódi LVGL-előnézetek és csomagellenőrzés.
- [x] Kompakt webes szerkesztő, ikonrendezés és mentési regressziós ellenőrzés.
- [x] README és használati dokumentáció.
- [x] Elérhető helyi tesztek, firmware/szimulátor ellenőrzés, korlátok rögzítése.

Ág: `feature/electric-home`, alap: `463d4426372b1b17a8018020d0b7edf66519005e`.
Az új téma csak Master/Loxone működést kap; a külön 720×720-as 4B firmware nem használ SD-s témakatalógust.

## Ellenőrzési állapot – 2026-10-29

- `tests/run_tests.py`: a teljes helyi csomag sikeres. A helyi konténerben a
  LeakSanitizer nem tudta olvasni a folyamatok `/proc` adatait, ezért a futás
  `ASAN_OPTIONS=detect_leaks=0` mellett történt; a cím- és UB-ellenőrzések megmaradtak.
- `tests/test_templates.py`: az összes csomag, a két háttér, a PNG-korlátok,
  Loxone/más mód aktiválása, módváltás, mentett választás és lapozás sikeres.
- `tests/test_loxone_home.py`: a tényleges C-modell és az autótöltő-összesítés sikeres.
- `tests/test_loxone_home_web.js`: a valódi webes szerkesztő mentése, ikoncseréje,
  ABC-rendezése, nagyobb előnézete és korábbi hibakezelése sikeres.
- A Master tényleges LVGL UI-ja lefordult, az előnézeti regresszió normál és
  RGB565 módban sikeres. Töltő nélkül minden autós elem és mozgó pont rejtett;
  valódi nulla, megszakadt kapcsolat, összegzett töltés és HU/EN feliratok ellenőrizve.
- A csomagelőnézetek a végleges téma tényleges LVGL-képkockáiból készültek.
- A külön 4B Slave nem használja ezt a témakatalógust, így firmware-kódja nem változott.
- ESP-IDF nincs telepítve a helyi környezetben: a Master/Slave firmware-buildek,
  a flash/OTA binárisméret és a Windows-szimulátor ellenőrzése a CI feladata.
- Fizikai panelen még szükséges az energiautak, a kábelív, a témaváltási idő
  és a webes elrendezés asztali/mobil böngészős próbája. Headless böngészőt a
  helyi környezetben nem sikerült telepíteni, a webes viselkedés DOM-teszttel ellenőrzött.
