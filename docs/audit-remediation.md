# Specifikációs audit – javításjegyzék

Azonosítók: a 2026-10-05-i specifikáció/kód audit A01–A27 tételei.

| Audit | Megvalósított javítás |
|---|---|
| A01–A02 | A UI statisztikai snapshot statikus tárba, a JSON napbetöltés heapre került. A 8 KiB feletti alkalmazásbeli veremkeret fordítási hiba. |
| A03 | A Pico C JSON formátum idézőjelei escape-elve; a generált 12 mező automatikusan ellenőrzött. |
| A04–A05 | 16 ms-os, eltelt idő alapú animáció; előjelváltáskor folytonos pöttyfázis. |
| A06 | Éjszakai tónusok a hasonlításoknál, hőmérsékletnél és inaktív/szigetüzemi ágaknál is. |
| A07–A10 | Saját vektoros chart: folyamatos görbék, nulla felé halványuló kitöltés mindkét grafikonon, közös geometria, sűrű pontjelölők nélkül. |
| A11 | Öt külön, rácshoz igazított időfelirat. |
| A12 | Valódi Noto Sans ő/ű és nagybetűs változatok hét méretben; eltávolított kézi ékezetpótlás. |
| A13–A14 | SD-jelzés a fejlécben; elkülönített összehasonlítás/magyarázat sorok; értékfüggő betűméret. |
| A15 | A tényleges firmware UI headless LVGL renderje CI-ben. A fizikai paneles ellenőrzés még szükséges. |
| A16 | Wi-Fi/MQTT állapotfigyelés már az indulási várakozás közben. |
| A17 | HU/EN webes hibák, resetokok és események; helyes állapotszövegek. |
| A18 | Éjféli integrálás az MQTT- és háttértask sorrendjétől függetlenül, két napra szétosztva. |
| A19 | SD-helyreállításkor mindkét megőrzött nap mentése. |
| A20 | 5 másodperces, írás nélküli SDMMC státuszellenőrzés, 60 másodperces újracsatlakozás. |
| A21 | SD I/O külön mutexen; rövid RAM-snapshot zárolás; mintafeldolgozás mentés közben is. |
| A22 | Revision szerinti fő/tmp/bak kiválasztás és helyreállított fájl előléptetése a következő mentés előtt. |
| A23 | Szigorú teljes JSON-, mintaszám-, teljesítmény- és energiaösszeg-validálás. |
| A24 | Konfigurálható mentési idő 5–60 perc. |
| A25 | Host regresszió, UBSan, Pico C ellenőrzés és LVGL előnézet CI-ben. A hardveres próba külön szükséges. |
| A26 | Taskindítások eredményének ellenőrzése és látható hibakezelés. |
| A27 | OTA-fogadás 30 másodperces előrehaladási timeouttal és megszakításkor erőforrás-felszabadítással. |

## Automatizált ellenőrzés

`python3 tests/run_tests.py` – C fordító és cJSON megosztott könyvtár szükséges (Ubuntu: `libcjson-dev`). A 13 tesztcsoport a production parser/config/state/events/stats kódot futtatja UBSan mellett, többek között előjelváltással, kétféle éjféli eseménysorrenddel, SD-helyreállítással, mentés közbeni mintával, megszakadt mentéssel és sérült fájlokkal. A Pico C példából a tényleges formátumsztringet fordítja és ellenőrzi.

A `ui-preview` CI job a production `app_ui.c`, `app_chart.c`, PNG-k és fontok használatával hat 1280×800-as képet készít: magyar nappali, szigetüzem/SD-hiba, feltöltés, rendszerhiba, angol éjszakai és Wi-Fi-hiba állapot. A képek a workflow artifactjai között találhatók. A firmware job ESP-IDF 5.5.4 alatt ESP32-P4 célra fordít.

Font újragenerálás: `python3 tools/prepare_fonts.py --help`. A Noto Sans Regular TTF szükséges; a generált C fájlok rögzítik a forrás SHA-256 lenyomatát, a licenc a `main/fonts/OFL.txt` fájlban található.

## Hardveren ellenőrizendő

A CI nem igazolja a fizikai Waveshare kijelző fényerejét, a képkockasebességet, PSRAM-terhelést vagy az SD-kártya tényleges eltávolításának viselkedését. Még szükséges a paneles képi összevetés, SD-eltávolítás/visszahelyezés, megszakított OTA és rollback, valamint egy legalább 24 órás, éjfélen áthaladó próba valós MQTT-adatokkal.
