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

## Teljesítmény – kötelező szabályok

Ezek a szabályok a v3 lassú téma- és oldalváltásainak tényleges okaiból
származnak ([részletek](docs/performance.md)). Új UI- vagy SD-kódnál mindegyiket
ellenőrizd, és ne vezesd vissza a régi mintákat.

- **Mindkét firmware-t nézd át.** A 10.1-es Master (`main/`, a Master Slave
  módjával együtt) és a Waveshare 4B Slave (`firmware/slave-waveshare-4b/ui`)
  külön UI-kódot használ. Egy UI-javítás akkor kész, ha mindkét célon
  ellenőrizted, hogy érvényes-e, és ha igen, alkalmaztad.
- **SD-olvasás:** nagy fájlt ne olvass `fread()`-del. Használd az
  `app_templates.c` `read_file()` függvényét (POSIX `read()`, 128 bájtra
  igazított PSRAM-puffer). A 128 bájtos newlib stdio-puffer és az igazítatlan
  cél miatt az SDMMC szektoronként külön parancsot küld.
- **Nincs kétszeres ellenőrzés vagy dekódolás.** A PNG teljes (zlib)
  ellenőrzése telepítéskor és `scan()`-kor fut. Futásidőben a változatlan
  csomagot az ujjlenyomat azonosítja, és elég a chunk-CRC. Ha a képet
  közvetlenül utána az LVGL lodepng-dekódere teljesen kibontja (az a CRC-t és
  az Adler-32-t is ellenőrzi), ne bontsd ki előtte még egyszer.
- **Változatlan erőforrást ne olvass újra.** Például az Otthon-kártyák
  lapozásakor a háttér és a logó dekódolt puffere megmarad, csak az ikonok
  töltődnek be. Hibánál viszont mindig teljes, tiszta újratöltés kell.
- **Periodikus frissítésben (Master 200 ms, 4B 33 ms) LVGL-értéket csak
  változáskor írj.** Az `lv_label_set_text()`, az `lv_image_set_src()` és az
  `lv_obj_set_style_*()` azonos értéknél is újrarajzol. Szöveghez használd a
  `label_text_if_changed`/`text_if_changed`, színhez és opacityhez a
  `*_if_changed` segédet, képhez az `lv_image_get_src()` összevetést. A pozíció-,
  méret-, flag- és állapotbeállítók maguk is kihagyják a változatlan értéket.
- **Drága lekérdezés ne fusson a UI-ciklusban feleslegesen.** Ilyen a
  heap-bejárás (`heap_caps_get_largest_free_block`), a Wi-Fi-lekérdezés
  (`esp_wifi_*`, ami az `esp_hosted` miatt SDIO-s RPC a C6 felé) és a
  taskstatisztika. Ezek csak akkor fussanak, ha a látható nézet használja őket.
- **Hosszú SD-munka ne fusson a témabetöltés vagy a kijelzőzár alatt.** A
  tárolózárat a webes feltöltés és az OTA is használja: percekig tartó
  dekódolás vagy több MiB-os kiírás ott megakasztja a feltöltést („SD-kártya
  írási hiba”). Az ilyen munkát külön, alacsony prioritású háttérfeladat végezze
  lépésenként, a zárat fájlonként elengedve, és lapozás/telepítés után rövid
  szünettel.
- **A témaváltás fázisait mérd az eszközön.** A desktop mérés nem mutatja a
  valódi SD- és dekódolási időt; a kijelző váltásonként egy összegző sort ír a
  naplóba és a webes eseménynaplóba (betöltőkép, SD, csere, felépítés, első
  képkocka). Optimalizálás előtt ezt nézd meg.
- **Ne késleltesd a megjelenítést írással vagy fix várakozással.** A
  SD-metaadatírás (például a `selection.txt`) a megjelenítés után fusson.
  Felhasználói inputra a runtime task ne fix `vTaskDelay`-jel várjon: a
  gesztus a bináris szemaforral ébresszen. Task-értesítést ne használj, mert
  ütközhet az IDF-meghajtókkal.
- **Feladatverem ne kerüljön PSRAM-ba.** Az `xTaskCreateWithCaps(...,
  MALLOC_CAP_SPIRAM)` ezen a hardveren indulás közben újraindulást okozott
  (`template_raw`, 2026-10-28), ezért SD-t, flash-t vagy DMA-t érintő
  feladatnál maradjon a belső RAM-os `xTaskCreate`. Újbóli próbához soros
  konzolos pánik-naplóval kell igazolni az okot.
- **SD-hiba ne blokkolja a kiutat.** A firmware aktiválása és az újraindítás
  akkor is fusson le, ha a statisztika mentése nem sikerült; a hiba naplóba
  kerül. Különben hibás kártyánál csak hardveres reset marad.
- **Ezeket szándékosan ne kapcsold ki:** a témaváltáskori chunk-CRC-t (csak ez
  biztosítja a Rendszer nézetre való visszaesést sérült képnél), a
  `template.json` sémaellenőrzését és a 85 ms-os betöltőkép-várakozást. Utóbbin
  az F12/F13 előzmények miatt csak fizikai panelen mérve szabad változtatni.
- **Mérj és tesztelj.**
  - Üresjárati frissítésre írj `LV_EVENT_INVALIDATE_AREA` számláló tesztet
    (elvárt: 0), és ellenőrizd, hogy a régi kódon megbukik.
  - Témaváltásra használd a `ZOLEE_THEME_SWITCH_BENCH=1` preview-módot.
  - Részleges frissítésnél a mentett képek ±1–2 egységgel eltérhetnek a
    teljes újrarajzolástól. Képpont-azonosságot ilyenkor a mentés előtti
    `lv_obj_invalidate(lv_screen_active())` és `lv_refr_now()` hívással,
    teljes újrarajzolással igazolj.
  - Az eredményt ne nevezd bájtazonosnak vagy gyorsabbnak, amíg nem mérted.

## Ellenőrzés

- A célzott változás előtt és után ellenőrizd, hogy a műveletek, konstansok, stringek és előfeldolgozó direktívák változatlanok.
- Futtasd a `python3 tests/run_tests.py` teszteket, a firmware ESP-IDF buildjét, valamint az érintett szimulátor- és regressziós teszteket, ha a környezet elérhető.
- Ellenőrizd az alkalmazás bináris méretét és a partíciós/OTA korlátokat; méretösszehasonlítás nélkül ne állítsd, hogy a két firmware bájtra azonos.
- Formázási célú PR nem tartalmazhat funkciómódosítást, konfigurációváltozást, tömörítetlen webes állományt vagy új beágyazott erőforrást.
