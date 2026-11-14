# Az új Otthon-kártyák dokumentációs képei

A hét PNG a [Loxone-kártyák útmutatójának](../../loxone-home-controls.md)
előnézete. A firmware valódi LVGL-kódját futtató
`tests/preview/controls_test.inc` renderelte őket szimulált Loxone-állapotokkal.
Nem készülékfotók és nem külön megrajzolt látványtervek.

## Eredet és formátum

A hat eredeti kép a `feature/loxone-home-controls` fejlesztés
2026-11-13-i helyi preview futásából származik. A 2026-11-14-i ellenőrzés
újrafordította az aktuális UI-t ASan-műszerezéssel: mind a hat korábbi PNG
képpontonként egyezett a friss PPM-kimenettel, ezért ezek a fájlok
változatlanok maradtak. A Slider részletes ablakának hetedik képe ekkor
került a dokumentációba, ugyanebből a tényleges LVGL-renderből. A próba
`ZOLEE_CONTROLS_ONLY=1 ZOLEE_PREVIEW_RGB565=1` módban futott, az aktuális
v4 `loxone-home.zip` SD-erőforráscsomaggal. Az alap kártyaoldal mindkét
témáját, a dimmer, a Slider, az időzítő, az összesítő és a motoros ablak
részletes kezelőjét mutatják.

| Blokk | Dokumentációs kép |
| --- | --- |
| Mind a hét általános új kártya | [Nappali oldal](controls-cards-day.png), [éjszakai oldal](controls-cards-night.png) |
| Dimmer | [Közvetlen fényerőállítás](controls-dimmer-day.png) |
| Slider | [Beállításra váró célérték](controls-slider-day.png) |
| TimedSwitch | [Időzítő és Tartós be](controls-timer-day.png) |
| WindowMonitor | [Összesített állapotok és részletlista](controls-monitor-day.png) |
| Window | [Célpozíció és Stop](controls-window-day.png) |
| CentralLightController, Gate | A fenti közös oldalképeken; ezekhez nincs külön felugró kezelő. |

A nyolcadik új blokk, az AudioZoneV2 képei a
[külön Audio-képtárban](../audio/README.md) vannak.

A natív **1280×800** méret megmaradt. A PPM → PNG átalakítás
veszteségmentes; a visszaolvasott PNG RGB-képpontjait a forráséhoz
hasonlítjuk. A képek dokumentációs fájlok, nem kerülnek a firmware-be
vagy az SD-csomagba.

## Frissítés

Fordítsd le a `tests/preview` `ui_preview` célját a CI-ben használt LVGL-lel,
majd külön kimeneti könyvtárban indítsd a fenti két kapcsolóval. A
`ZOLEE_HOME_RESOURCE_ZIP` az elkészített Otthon ZIP abszolút útvonala,
a `ZOLEE_TEMPLATE_ROOT` a teszt számára fenntartott telepítési könyvtár.
Az eszköz a PPM-eket az aktuális munkakönyvtárba írja.

A képátalakítás Pillow-val:

```python
from pathlib import Path
from PIL import Image

names = (
    "controls-cards-day", "controls-cards-night", "controls-dimmer-day",
    "controls-slider-day", "controls-timer-day", "controls-monitor-day",
    "controls-window-day",
)
source = Path(".preview-images")
output = Path("docs/images/controls")
output.mkdir(parents=True, exist_ok=True)
for name in names:
    with Image.open(source / f"{name}.ppm") as image:
        rgb = image.convert("RGB")
        assert rgb.size == (1280, 800)
        target = output / f"{name}.png"
        rgb.save(target, optimize=True, compress_level=9)
        with Image.open(target) as saved:
            assert saved.convert("RGB").tobytes() == rgb.tobytes()
```

A PNG-ket vizuálisan is ellenőrizni kell. A teszt a hét kártya eseményeit,
a pontos −/+ léptetést és tartományhatárait, a cél és a visszajelzett
érték különválasztását, az iránytiltást, az állapot szerint rendezett,
256 sorig lapozható összesítő kezelőjét, az elavult ablakok elutasítását,
a régi ikoncsomag képhiányának kezelését és a változatlan állapotnál
elvárt nulla LVGL-invalidációt is ellenőrzi. A nagy lista korlátját a
modell korlátozza; a felületi fixture 14 soron ellenőrzi az első,
köztes és utolsó oldal működését.
