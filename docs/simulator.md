# Windows UI-szimulátor (Visual Studio 2026)

A szimulátor ugyanazt az `app_ui.c`, `app_chart.c`, PNG-ikonokat és betűkészleteket használja, mint az ESP32 firmware. A 12 kötelező mezőt és az opcionális SunPosition adatot tartalmazó JSON-csomag a valódi `app_ingest_process` → `app_payload_parse` → `app_state_set_sample` útvonalon érkezik. Nincs szükség eszközre, MQTT-brokerre, ESP-IDF-re vagy SD-kártyára.

## Indítás F5-tel

1. Töltsd le/checkoutold az ezt a fejlesztést tartalmazó ágat.
2. Visual Studio: **File → Open → Folder**, válaszd a repository gyökérmappáját.
3. Válaszd a **Windows UI demo (Debug, x64)** CMake-konfigurációt (`windows-demo-debug`).
4. Várd meg a CMake konfigurálást. Első alkalommal internet kell: automatikusan letölti az LVGL 9.5.0, cJSON 1.7.19 és zlib 1.3.1 forrását. Python, SDL és külön vcpkg-telepítés nem szükséges.
5. Az indítási cél legyen **zolee_simulator.exe**, majd **F5**.

Ha a preset nem jelenik meg: **Tools → Options → CMake → General** alatt engedélyezd a CMakePresets.json használatát, és nyisd újra a mappát. Töréspont tehető például a `main/app_payload.c`, `main/app_ui.c` vagy `simulator/simulator.c` fájlba.

Visual Studio Installer komponensek: C++ core features; MSVC Build Tools for x64/x86 (Latest); C++ CMake tools for Windows; Windows 11 SDK. A szimulátor natív Win32/GDI ablakot használ.

## Demó működés

Az első adatcsomag induláskor, a továbbiak 5 másodpercenként érkeznek. Kilenc állapot ismétlődik: napenergia és export/töltés; hálózati vételezés/töltés; akkumulátoros fogyasztás; eső; hó; éjszaka; felkészülés/meleg akkumulátor; szigetüzem; rendszerhiba/forró akkumulátor. A csomagok teljesítményei energiamérleget tartanak. A natív ablak megtartja az 1280×800 képarányt átméretezéskor.

| Billentyű | Művelet |
|---|---|
| Balra/jobbra nyíl vagy vízszintes egérhúzás | Előző/következő sablon |
| D | Következő demó állapot, azonnali csomag |
| Space | Automatikus adatküldés szüneteltetése/folytatása; 30 másodperc után HTTP-adathiány képernyő |
| N | Nappali/éjszakai mód kézi felülbírálása, azonnali csomag |
| W | Wi-Fi kapcsolat megszakításának/helyreállásának szimulációja |
| S | SD-hibajelzés kapcsolása |
| Esc | Kilépés |

A kézi állapot-/témaváltás szünet közben is küld csomagot, ezért újraindítja a timeoutot. Folytatáskor legfeljebb 5 másodperc múlva érkezik a következő csomag. A konzolban látható az elküldött JSON. A demó jelölés az ablakban és a kijelző címében is szerepel.

A kilenc SD-s sablon konfiguráláskor a build `simulator/templates` mappájába másolódik.
A futási munkakönyvtár legyen a build `simulator` mappája; ott a `templates`
alkönyvtár tárolja a választást. A `ZOLEE_TEMPLATE_ROOT` környezeti változóval
más tesztkönyvtár is megadható. A forráscsomagok így nem változnak meg.
A `ZOLEE_DISABLE_RAW` környezeti változóval az előre dekódolt kép-segédfájlok
készítése és használata kikapcsolható; így a PNG- és a segédfájl-út képkockái
összevethetők. A firmware-be ez a kapcsoló nem kerül bele.

A sablonok nem váltanak automatikusan. Az N billentyű továbbra is a demó
adatcsomag `NightMode` mezőjét változtatja. A webes feltöltés firmware-funkció;
a szimulátor nem indít HTTP-szervert.

A grafikonok és a tegnapi összehasonlítás szintetikus előzményt kapnak, az órás diagram integrált slotenergiával, a Nap pálya T1-demóadattal és 15 kWh-s akkukapacitással működik; az új minták memóriában frissítik a demó statisztikát. Ez nem az SD-statisztika/tartós mentés tesztje. A szimulátor nem nyit hálózati végpontot. A hardveres fényerő, OTA, Wi-Fi és SD-kezelés itt adapterrel helyettesített. Az ESP-IDF fordítás alapértelmezése változatlan; a demó csak `ZOLEE_SIMULATOR=ON` mellett fordul, sem Debug, sem Release firmware-be nem kerül bele.

## Parancssori ellenőrzés

Visual Studio Developer PowerShellből:

```powershell
cmake --preset windows-demo-debug
cmake --build --preset windows-demo-debug
ctest --preset windows-demo-debug
```

A `zolee_simulator.exe --self-test` ablak nélkül ellenőrzi a demó csomagokat, érvénytelen JSON elutasítását, timeoutot, helyreállást és az animációt; `simulator-demo.ppm` képet ment. Ugyanezt MSVC Debug fordítással a Windows CI is futtatja. A headless teszt Linuxon is fordítható `cmake -S . -B out/sim -DZOLEE_SIMULATOR=ON` paranccsal.

Az ablak pufferelt GDI-rajzolást használ: a háttér és a kijelző képe memóriában áll össze, majd egyetlen másolással jelenik meg. Az animációk közben nincs teljes ablakos fekete háttértörlés. A Windows CI ezt a megjelenítési utat is ellenőrzi ismételt rajzolással és méretváltással.

Az **I** billentyű a közös diagnosztikai nézetet nyitja/zárja: balra tömör adatok, jobbra a legfrissebb 15 RAM-naplóesemény. A statisztikai téma demó SOC- és forrásenergia-adatokat, valamint hiányzó napot jelölő heti/havi oszlopdiagramokat is mutat.

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

## Loxone Otthon bemutató (v3 fejlesztési ág)

A firmware két új Master-oldala ugyanazzal az LVGL-kóddal próbálható.
A GitHub Actions Windows-artifactja már tartalmazza a közös erőforrásokat.
A kicsomagolt szimulátor könyvtárából:

```powershell
.\zolee_simulator.exe --home-demo
```

Fogyasztói oldalról X-szel visszatérhetsz az energiaoldalra; a ház érintése újra
megnyitja, lefelé húzás az Otthon-kártyákat mutatja. A demó gombjai nem kapcsolnak
valódi Loxone-eszközt. A valódi hálózati parancsút külön C/protokolltesztekkel fut.

Saját buildhez a repó gyökeréből készítsd el és bontsd a szimulátor másolt SD-jébe:

```powershell
python tools/prepare_home_resources.py
python -m zipfile -e dist/templates/loxone-home.zip out/simulator/simulator/templates/loxone-home
```

A `--home-demo` már három címezett Otthon-lapot mutat: Földszint, Kert és egy üres
Vendégszoba. Jobbra/balra húzással lapozható; a fejléc és a pontok követik az
aktuális oldalt. A fogyasztói oldal külön külső háznézetet, az Otthon beltéri
hátteret használ. Az alsó szöveg mindkettőn fedő, kontrasztos láblécen jelenik meg.

## Akku diagnosztika demó

A `10-battery-diagnostics` témát a többi téma között lehet kiválasztani.
A desktop három mesterséges töltés/kisütés-párt és egy élő kisütést a valódi
C-modelllel számol ki (15 kWh névleges, kb. 13,8 kWh / 92% becslés). Ezek
demóadatok, nem csatlakoztatott akkumulátor mérési eredményei. A készüléken
ugyanezek a mezők csak a tényleges folyamatos mérések után telnek ki.
