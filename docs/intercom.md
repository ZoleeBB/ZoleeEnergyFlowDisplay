# Loxone kaputelefon – Master kijelző

A Loxone adatfogadási módban működő Master a kaputelefon közvetlen HTTP/MJPEG
élőképét jeleníti meg. A Loxone 100484 Intercom White helyi videócímét a felhasználó
böngészőben kipróbálta: `/mjpg/video.mjpg`. Más készüléknél kompatibilis MJPEG-cím kell.
A funkció alapértelmezetten ki van kapcsolva, a Slave kijelzőkre nem továbbít videót.

## Beállítás

A **Loxone beállítások → Kaputelefon (Master kijelző)** részben:

1. Engedélyezd a kaputelefont.
2. Add meg a teljes helyi videócímet, például
   `http://192.168.1.50/mjpg/video.mjpg`. Ez a kaputelefon IP-címe, nem a Miniserveré.
3. Válaszd ki a **Csengetés folyamatban** állapotot a Loxone adatlistából.
   A helyiség és funkció szűrő az Otthon-szerkesztővel közös.
4. Opcionálisan válassz **Kapunyitás** parancscélt és adj meg gombfeliratot.
   Ugyanazok a nem védett Switch / Pushbutton / TimedSwitch objektumok választhatók,
   mint az Otthon pillanatnyi gombjánál. A visszajelző állapot UUID-je nem parancscél.
5. Mentsd el a kaputelefon és Otthon beállításait a közös mentőgombbal.

Külön kamerafiók nincs: a videókapcsolat automatikusan a **mentett Loxone integráció
felhasználónevét és jelszavát** használja HTTP Basic hitelesítéssel. A fióknak a
kaputelefon közvetlen videóeléréséhez is megfelelő jogosultság kell. A jelszó nem
kerül a videócímbe, az Otthon JSON-ba vagy a webes állapotválaszba; továbbra is a
meglévő titkosított Loxone-tároló kezeli. A HTTP videót helyi, megbízható hálózaton
használd. Átirányítást nem követünk, így más cél nem kaphatja meg a hitelesítést.

## Megjelenés és működés

- A kiválasztott numerikus/digitális állapot **1 / true** értéke teljes képernyős,
  fekete hátterű, képarányt megtartó videónézetet nyit, az aktuális oldal fölött.
- Kizárólag ugyanennek az állapotnak a **0 / false** értéke zárja be a nézetet,
  és teszi újra láthatóvá a korábbi oldalt. Nincs bezáró gesztus vagy időzítő.
- Ismeretlen, hibás, más UUID-hez tartozó állapot vagy kapcsolatvesztés nem nulla.
  A nézet ilyenkor megmarad, és szükség esetén hibajelzést mutat.
- Kapunyitó cél nélkül nem jelenik meg gomb. Konfigurált célnál a videó alján
  egy 480×92 képpontos, nagy **Kapunyitás** gomb jelenik meg az egyedi felirattal.
- A gomb érintésenként egy `pulse` parancsot ad a közös Loxone-kapcsolaton.
  Hosszú nyomás nem ismétel, kapcsolatvesztéskor nincs későbbi automatikus újraküldés.
  A kapunyitás **nem zárja be** a kameraképet. Offline vagy nem támogatott célnál
  a gomb letiltott; a parancsot a worker ismét ellenőrzi elküldés előtt.
- Videóhiba esetén a háttérfeladat újrapróbálkozik. Hibás kamerahitelesítésről,
  memóriahiányról és nem támogatott képről felirat tájékoztat.

## Erőforrások és korlátok

A videó külön, alacsonyabb prioritású CPU0-feladatban érkezik, nem blokkolja a
Loxone állapotfolyamot. A JPEG-dekódolást az ESP32-P4 hardvere végzi; legfeljebb
10 képkockát jelenítünk meg másodpercenként. Az adatfolyamot folyamatosan ürítjük,
nincs felhalmozódó videósor. Egy tömörített képkocka legfeljebb 256 KiB, a kép
legfeljebb 1280×720, baseline színes JPEG legyen. A videópufferek csak csengetéskor
foglalódnak: a megjelenített RGB565 kép kb. 1,76 MiB PSRAM, a dekóder eredménye
legfeljebb ugyanennyi, továbbá 256 KiB a tömörített keret. A megjelenített képet
és a dekóder kimenetét külön puffer őrzi, így a rajzolás nem olvas éppen átírt képet.

A konfiguráció a meglévő `loxone-home.json` profil opcionális `intercom` objektuma,
a korábbi profilok alapértelmezetten kikapcsolt kaputelefonnal töltődnek be.
Nincs új SD-erőforráscsomag vagy új kép-/ikonletöltés.

A böngészős kameraelérés igazolt; a firmware valós képfrissítése, a tényleges
kaputelefon JPEG-formátuma és a teljes rendszer együttes terhelése hardveres próbát igényel.

## Fejlesztői ellenőrzés

- `python3 tests/run_tests.py`: teljes helyi regressziós tesztcsomag.
- `python3 tests/test_intercom.py`: tényleges, korlátos MJPEG-keretező és a
  csengetésállapot, konfigurációs roundtrip, hibás címek és kapunyitó parancscél.
- `node tests/test_loxone_home_web.js`: a tényleges szerkesztő kódja, opcionális
  kapunyitás, közös mentés és a hitelesítési adatok kizárása.
- A szimulátor `simulator_intercom` CTest tesztje a tényleges LVGL-felületen
  ellenőrzi a gomb nélküli hívást, a nagy kapunyitót, az egyetlen impulzust,
  a régi kép elutasítását, a hibánál megmaradó nézetet és a felszabadított puffert.
- `tests/test_loxone_protocol.py`: a tényleges közös Loxone-parancssoron azt is
  ellenőrzi, hogy a sorba állítás után véget érő csengetéshez már nem küldünk impulzust.

Az ESP-IDF firmware-build, a bináris-/OTA-méret és a valós kaputelefon együttes
hardveres ellenőrzése szükséges a kiadás előtt.
