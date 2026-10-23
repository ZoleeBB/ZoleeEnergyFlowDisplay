# Akku diagnosztika – fejlesztési állapot

Ág: `feature/battery-diagnostics`, alap: `main`. A kaputelefonos PR külön marad.

- [x] Meglévő kapacitás-, előjel-, adatfogadási és statisztikai útvonal feltérképezése.
- [x] Korlátos memóriájú, passzív energia/SOC diagnosztikai modell.
- [ ] Bekötés minden helyi statisztikát számoló adatfogadási módba, atomi SD-mentés.
- [ ] Nappali/éjszakai 1280×800 téma, tömörített háttér, értékelés és adathiány.
- [ ] Tényleges LVGL-előnézetek, szimulátor, README és használati leírás.
- [ ] Modell-, tárolási-, témacsomag- és regressziós ellenőrzések; build/méretkorlát.
- [ ] Commitok feltöltése, PR és CI ellenőrzése.

Mérési szabályok: PowerBat kW, negatív töltés, pozitív kisütés. Energia kWh.
Legalább 20 százalékpont/10 perc folytonos mérés; hét lezárt szakasz, 30 napos
érvényesség; kisütések mediánja. A kapacitás becslés, nem igazolt BMS-SOH.
Napi energiából nem következtetünk romlásra. Hatásfok csak folytonos, szomszédos,
azonos SOC-ablakú töltés/kisütésből. Nincs invertervezérlés vagy kényszerciklus.
Újraindítás/adatkimaradás után nem hidalunk át hiányzó energiát. A diagnosztika
a téma kiválasztásától függetlenül fut. SD nélküli Slave-en nincs ilyen téma.
