# Grafikai assetek

A teljes készlet **29 átlátszó PNG-ikonból** áll. A 2026-10-05-i csere minden
régi kivágásos PNG-t és a futásidejű SVG-állapotikonokat is kiváltja.
A jóváhagyott látványterv vizuális referencia, nem kivágási alapanyag.

## Források és feldolgozás

- `assets/source/`: a 14 korábban elfogadott ikon eredeti képei (az összefoglalók
  közös nyolcikonos képlapon), illetve 15 új, önállóan generált ikon.
- `assets/source/manifest.json`: forrás → futásidejű ikon megfeleltetés,
  az elfogadott képlap cellái és az új képek teljes generálási promptjai.
- `main/assets/png/`: középre igazított, 128×128 RGBA képek.
- `tools/prepare_assets.py`: az elfogadott képlap szétválasztása, átlátszó
  margók normalizálása és aránytartó kicsinyítés ImageMagickkel.
- `docs/previews/icons.png`: a teljes futásidejű készlet áttekintő képe.

Az eredeti illusztrációkat a feldolgozás megőrzi. Nem rajzol új ikonokat,
nem vág ki semmit a dashboard látványtervéből. Az új grafikák a beépített
imagegen eszközzel készültek, nem SVG-ről raszterizált helyettesítők.

Újraépítés: `python3 tools/prepare_assets.py` (ImageMagick szükséges).

## Teljes készlet

| Csoport | Fájlok (`.png`) |
| --- | --- |
| Fejléc | `header_home_leaf` |
| Napi kártyák | `metric_leaf`, `metric_house`, `metric_grid`, `metric_coins` |
| Energiaábra | `flow_battery`, `flow_grid`, `flow_house`, `flow_hub`, `flow_solar` |
| Összefoglalók | `summary_co2`, `summary_tree`, `summary_bulb` |
| Időjárás | `weather_sun`, `weather_cloud`, `weather_partly`, `weather_rain`, `weather_snow`, `weather_moon`, `wind` |
| Rendszerállapot | `status_normal`, `status_prepare`, `status_island`, `status_error` |
| Kapcsolat és adatok | `grid_off`, `wifi_off`, `mqtt_off`, `waiting_data`, `timeout` |

## Firmware-integráció

Mind a 29 PNG a firmware-be ágyazott, LVGL 9.5 LodePNG dekóderrel jelenik meg.
A CMake automatikusan méri a tényleges fájlméreteket és újrakonfigurál, ha egy
PNG változik. Nincs kézzel karbantartandó bájtszám, ami képcserénél elavulhat.
A nagyfelbontású források nem kerülnek a firmware-be.

Az elavult `assets/svg/` készlet kivezetésre került; a Git előzményekben
visszakereshető. A program egyetlen felismerhető ikonhoz sem használ SVG-t.

## Dinamikus körvonalak

A négy energia-node színes kerete nem a PNG része. Natív LVGL körvonal,
amely a saját összekötőjének pontos színét kapja, nappali és éjszakai módban is:

- PV: zöld; házfogyasztás: narancs.
- Hálózat: vételezés narancs, visszatáplálás zöld.
- Akkumulátor: kisütés narancs, töltés zöld.
- Nulla közeli teljesítmény: semleges, a vonallal egyező szürke.
- Szigetüzem: hálózat szürke, energiapontok nélkül.

A töltöttségi sáv külön SOC-jelzés. A SOC nem módosítja a körvonal színét.
A kártyák puha körháttere, energiaösszekötők, mozgó pontok, grafikonok és
SOC-sáv natív LVGL elemek maradnak.
