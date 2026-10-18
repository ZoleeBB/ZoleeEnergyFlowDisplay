# Fejlesztői instrukciók – Zolee Energy Flow Display

Ezek az elvek minden további firmware-fejlesztésre és kódmódosításra érvényesek.

## Forráskód és dokumentáció

- A kézzel írt C/C++ források és fejlécek legyenek jól olvashatók: 4 szóközös behúzás, átlátható függvények, logikusan elválasztott blokkok, normál sortörések, következetes szóközök. Ne tömöríts több, egymástól független utasítást egy sorba.
- A kommentek **magyarul** készüljenek. A modul, a nem nyilvánvaló döntések, az időzítési/erőforrás-korlátok, a hibakezelés és az adatok irányának magyarázata fontosabb, mint a triviális utasítások ismétlése.
- A publikus API-k nevei, protokollazonosítók, konfigurációs kulcsok, naplóformátumok és felhasználói feliratok nem változhatnak puszta formázás miatt.
- Kódformázás és kommentelés önmagában nem változtathat működést. Különösen ügyelj az előfeldolgozó direktívákra, makrókra, stringekre, fordítási beállításokra és minden időkritikus LVGL/FreeRTOS kódrészre.
- A beépülő statikus erőforrásokat (például `assets/web`, `main/assets/png`, `main/fonts`, generált fejlécek, SD-s sablonok) **nem szabad** csak olvashatósági okból széthúzni, mert a bájtjaik növelhetik a firmware, illetve az SD-csomag méretét.
- A forráskód kommentjei és szóközei általában nem kerülnek a futtatható binárisba. Ettől függetlenül mérd vagy ellenőrizd a lefordított firmware méretét, és nem elfogadható a flash-/RAM-/PSRAM-keret túllépése. Ha méretnövekedés vagy funkcionális eltérés jelentkezik, az érintett formázási módosítást vissza kell vonni.
- Soha ne írj át kézzel generált vagy külső függőségből származó fájlt. Azokat a generátor vagy az upstream projekt kezeli.

## Ellenőrzés

- A célzott változás előtt és után ellenőrizd, hogy a műveletek, konstansok, stringek és előfeldolgozó direktívák változatlanok.
- Futtasd a `python3 tests/run_tests.py` teszteket, a firmware ESP-IDF buildjét, valamint az érintett szimulátor- és regressziós teszteket, ha a környezet elérhető.
- Ellenőrizd az alkalmazás bináris méretét és a partíciós/OTA korlátokat; méretösszehasonlítás nélkül ne állítsd, hogy a két firmware bájtra azonos.
- Formázási célú PR nem tartalmazhat funkciómódosítást, konfigurációváltozást, tömörítetlen webes állományt vagy új beágyazott erőforrást.
