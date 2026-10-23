# Akku diagnosztika – fejlesztési állapot

Ág: `feature/battery-diagnostics`, alap: `main`. A kaputelefonos PR külön marad.

- [x] Meglévő kapacitás-, előjel-, adatfogadási és statisztikai útvonal feltérképezése.
- [x] Korlátos memóriájú, passzív energia/SOC diagnosztikai modell.
- [x] Bekötés minden helyi statisztikát számoló adatfogadási módba, atomi SD-mentés.
- [x] Nappali/éjszakai 1280×800 téma, tömörített háttér, értékelés és adathiány.
- [x] Tényleges LVGL-előnézetek, szimulátor, README és használati leírás.
- [x] Modell-, tárolási-, témacsomag- és teljes host regresszió; 32 bites és RGB565 LVGL render.
- [ ] ESP-IDF firmware/build és tényleges OTA-méret: GitHub CI (helyben nincs ESP-IDF).
- [ ] Commitok feltöltése, PR és CI ellenőrzése.

Mérési szabályok: PowerBat kW, negatív töltés, pozitív kisütés. Energia kWh.
Legalább 20 százalékpont/10 perc folytonos mérés; hét lezárt szakasz, 30 napos
érvényesség; kisütések mediánja. A kapacitás becslés, nem igazolt BMS-SOH.
Napi energiából nem következtetünk romlásra. Hatásfok csak folytonos, szomszédos,
azonos SOC-ablakú töltés/kisütésből. Nincs invertervezérlés vagy kényszerciklus.
Újraindítás/adatkimaradás után nem hidalunk át hiányzó energiát. A diagnosztika
a téma kiválasztásától függetlenül fut. SD nélküli Slave-en nincs ilyen téma.


## Ellenőrzött állapot

- `python3 tests/run_tests.py`: sikeres, az új SD-/éjféli/boot tesztekkel.
- `ui_preview`: sikeres 32 bites és RGB565 render, 20 témanézet / 11 elrendezés,
  navigáció és 100 teljes Home-életciklus. Valódi modellből készült demómérés.
- `test_templates.py`, `test_template_packages.py`, Markdown-linkek és
  `git diff --check`: sikeres.
- A csomag a 10.1-es kijelzőhöz 1280×800-as háttérpárt és 480×300-as
  LVGL-előnézeteket tartalmaz. A felhasználó kérésére a laboros koncepciót
  fehér moduláris akkura, külső házfalra és természetes kertre cseréltük;
  nappal és éjjel ugyanaz az elrendezés, megváltozott világítással.
- A Master és a közös statisztikát fordító 4B firmware CMake-listája is
  tartalmazza az új számítási modult. A 4B nem kap új témát.
- A hordozható USB-csomag minden Markdown-dokumentumban átírja az
  SD-be áthelyezett téma-előnézetek hivatkozását; a valódi release-teszt védi.
