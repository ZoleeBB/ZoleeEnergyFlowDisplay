# v3 – követelmény- és záró ellenőrzés

Dátum: 2026-10-21. Alap: az [eredeti fejlesztési terv](v3-development-plan.md)
D01–D16 döntései és a későbbi F01–F14 javítások. A felhasználó az `adfa395`
firmware után igazolta a villogás megszűnését, a háttér megjelenését, a vezérlések
és az új témák működését, majd kérte az ellenőrzést és a PR megnyitását. Ezután F12-ként új, Home bezárása utáni navigációs/terhelési hibát jelzett; az F12 után pedig minden témán indulástól akadozást és elvesző húzást jelentett (F13). Az F13 fizikai próbája sikertelen: ritka húzás, akadozás, CPU0 28–52%, CPU1 kb. 1%. Az F14 a main részleges frissítéséhez tér vissza, koherens kész frame-ekkel és külön grafikus CPU-maggal; fizikai ellenőrzése nyitott.

## Követelmények és bizonyítékok

| Követelmény | Megvalósítás | Automatizált ellenőrzés |
| --- | --- | --- |
| D01–D03: csak Master/Loxone, egy meglévő kapcsolat | Mód szerinti web/API/UI-kapuk; nincs új WS vagy Slave-kártyavezérlés | `test_loxone_home_web.js`, `test_loxone_protocol.c`, valódi LVGL módváltások |
| Fogyasztók: név, ikon, állapot, szorzó, opcionális üzemi teljesítmény | Külön jelszómentes Home-profil, atomi SD-mentés, szerkesztő | `test_loxone_home.py`, `test_loxone_home_store.py`, tényleges HTTP/JS és titkosított WS-próbák |
| D04–D05: opt-in javaslat, előbb 100% akku | Konzervatív helyi asszisztens; 120 s valós PV-többlet, tartalék, nulla ismert fogyasztás; automatikus kapcsolás nélkül | `test_energy_assistant.c`, WS-snapshot és LVGL-asszisztenspróbák |
| Magasabb éjszakai fogyasztás | Lefedett órák, előző hét legalább három érvényes éjszakájának mediánja, szinkronizált idő | `test_core.c`, SD/reboot/hiányos adat/óraugrás, asszisztensmodell |
| D06/D10/D11: Slave-jelenlét és saját fényerők | Nappal saját nappali érték; éjjel opcionális sötétítés, jelenlét/érintés/timeout; első teljes érintés elnyelése | `test_slave_display.py`, `test_slave_presence.py`, `test_slave_presence_service.py`, két panel valódi input/PWM runtime-ja |
| Slave-azonosság, szerkesztő és tartós profil | Három bondolt identitás, név, forrás, időkorlát, CRC-s NVS, MITM-védett opcionális 1006 olvasás; régi Masterrel kompatibilis | NVS-/GATT-/replay/stale/handle-újrahasználati hibainjektálás, tényleges JS és Chromium desktop/mobil |
| D07: Nyugodt otthon és Energiakör újratervezése | Jóváhagyott nappali/éjszakai hátterek; 05/06 ID és sorrend megőrzött; 06 napállás-idővonal | HU/EN, nappal/éjjel és szélsőértékes LVGL-render; felhasználó eszközön elfogadta |
| D08: 96×96 ikonok és 1280×800 Master-hátterek | 64 átlátszó ikon, négy külön Home/fogyasztói háttér; nagy generálási eredetik nélkül | `import_home_icons.py --complete`, C/JS ZIP-validátor, valódi PNG-dekódolás |
| D09: olvashatóság és erőforráskeretek | Magyar kommentek az új modulokban; korlátos profil, sorok, képek, 8192 bájtos firmware-frame figyelmeztetés; managed fájlok helyett build-másolat | Fordítók/UBSan/ASan, partíció- és USB-csomagellenőrzés; számszerű hardverterhelés külön mérendő |
| D12: 1–12 fogyasztó, hiányos sor középen | Közös 4×3 geometria; 7 elemnél 4+3, rögzített kártyaméret | A valódi geometria minden 1–12 esete; tényleges hétkártyás LVGL-render |
| D13: érték, pulse és kapcsoló | 64 ikon, külön állapot/parancscél, visszajelzés szerinti kapcsoló, egyhelyes rövid életű parancssor, tiltott célok elutasítva | Valódi AES/RSA/HMAC út, 200 nyugta/állapot/timeout/reconnect; felhasználó igazolta a vezérléseket |
| D14: külön hátterek és olvasható lábléc | Fogyasztói/Otthon nappali/éjszakai képpár, fedő láblécpanel | Valódi render, a háttér képpontjainak összevetése, fizikai háttér-visszaigazolás |
| D15: elnevezhető több oldal | Schema 2: legfeljebb 8×12 kártya, schema 1 migráció, üres oldal, cím/pontok, saját vízszintes lapozás | Parser/mentés/JS, mindkét swipe-irány és körbefordulás; lapozás nem küld parancsot |
| D16: Energiakör napállás-idővonal | 00:00–24:00, napkelte/napnyugta, aktuális idő és érvényes napadatok | Tényleges LVGL/JSON/webes elemvalidálás, szélsőértékek és éjszakai mód |
| F12: visszatérés utáni navigáció és betöltés | Inputszintű, egyszer kötött gesztus; nem görgethető képernyő; valós loading-flush SD előtt; változatlan touch időzítő | Valódi X, alsó szélről diagnosztika, két témairány, blokkoló fedőelem, flush-sorrend, két színformátum; fizikai ellenőrzés nyitott |
| F13: indulástól akadozó témák és elvesző húzás | 15/16 ms refresh/animáció, frame-végi plusz várakozás nélkül; az ismételt touchpont nem törli a húzást, 50 px küszöb | Mind a tíz téma Home nélkül, lassú bal/jobb/felfelé húzás; javítás előtt reprodukált hiba, 15 ms redraw és 90 ms-os flush; fizikai elfogadás nyitott |
| F14: teljes rajzolási költség, érintés és CPU-elosztás | TRIPLE_PARTIAL, CPU-baseline minden új frame előtt, egyszeri kész-frame átadás; GUI/touch CPU1, Loxone worker CPU0 | Valódi részpufferes LVGL, FULL/PARTIAL pixelmunka-összevetés; 480 frame puffer/stride/forgatás/hibainjektálás a buildelt adapterrel; fizikai elfogadás nyitott |
| Diagnosztika, navigáció és párosításjelzés | Jobb felső X; lefelé húzás az Otthoné; tényleges párosítási ablakhoz kötött kék gomb és BT-ikon | LVGL X/gesztus/PIN/eseményterjedés és feliratközép-próbák |
| Külső témák és visszafelé kompatibilitás | Deklaratív elemek, korlátos ZIP/PNG/JSON, `home_hotspot`, `assistant_area`; régi csomagok támogatottak | `test_templates.py`, C/JS validátorok, külső témák tényleges LVGL-próbái |

Nem találtunk implementálatlan, elfogadott v3 termékfunkciót. A fenti tesztek
külön megkülönböztetik a szintetikus bemenetet és a tényleges C/JS/LVGL-kódot;
a szimulátor nem helyettesíti a rádiós vagy paneles terhelésmérést.

## F01–F14 és dokumentáció

F01/F02: csomagelőnézet, kiválasztott ikon és hiányzó csomag jelzése.
F03: friss worker-idő és a stabil objektumazonosítótól külön feloldott `uuidAction`.
F04/F08: minden bezárási út látható betöltőképpel, HU/EN szöveg és valódi spinner.
F05/F11: teljes Master-képkockák, elődekódolt RGB565 háttér és ARGB/szürke ikonok;
hibás kép teljes visszavonása és korlátos újrapróbálás. F06: a Rendszer-házkép
közepe is érinthető. F07: középre igazított BT-gomb. F09: azonnali törlési
folyamatjelzés és helyreálló webes kezelő. F10: egyértelmű CI/kiadási út és
pontosan négy letölthető csomag. Részletek: [javítások](v3-field-fixes.md).

Az audit javította a workflow-/artifactneveket és eseményeket, a régi Master
50 soros/részleges képpuffer leírását, a diagnosztika lefelé húzásos bezárását,
a Home-képek birtoklási/dekódolási leírását és az elavult „még nincs fizikai
visszajelzés” státuszokat. A történeti checkpointok az akkori állapotot őrzik.
Az aktuális forrás a README, architektúra, hardver, hibák, teljesítmény,
Loxone Home-formátum, kiadási út és [átadási lista](v3-handover.md) között egyeztetett.

## Ellenőrzés és kiadási státusz

- A kiinduló, eszközön elfogadott `adfa395` forrás minden CI-je sikeres:
  Master/Slave ESP-IDF 5.5.4, Windows/Linux, Chromium, natív/UBSan, két ASan-render.
- Master app: 3 051 264 bájt, 8 MiB partíción 64% szabad. Slave: 3 278 992 bájt,
  12 MiB partíción 74% szabad. Az OTA/USB eredet és minden SHA256 ellenőrzött.
- A lezárás az F12/F13 runtime-javítást, a workflow-kat, a kiadási útválasztót,
  regressziókat és leírásokat is érinti. Új grafikai csomag nem szükséges.
  Helyi teljes natív regresszió, actionlint, 9 sabloncsomag és 194 helyi
  dokumentációs link sikeres; ASan 32 bit/RGB565 külön 100 életciklussal és
  400–400 Home-oldallal sikeres. A helyi ASan hozzáférést vizsgál; a PR-CI
  detect_leaks=1 értékkel szivárgást is ellenőriz. Részletek: 26. checkpoint.
- A korábbi funkcionális panelpróba sikeres volt; az F12 panelpróba sikertelen lett, az F13 panelpróba is sikertelen lett, az F14 panelpróba nyitott. Tartós, számszerű CPU/FPS/
  belső RAM/DMA/PSRAM-mérés, három Slave egyidejű rádiós terhelése és a párosítási
  ablak külön panelmérése nincs friss mérési naplóval igazolva.
- A PR megnyitása kifejezett felhasználói kérés. Egyesítés, v3 tag vagy release
  nincs előre végrehajtva. Saját v3 ág igazolt main merge-je után az egyetlen
  automatikus kiadási út választja a v3.0.0 verziót.
