# Az új Otthon-kártyák dokumentációs képei

A hat PNG a [Loxone-kártyák útmutatójának](../../loxone-home-controls.md)
előnézete. A firmware valódi LVGL-kódját futtató
`tests/preview/controls_test.inc` renderelte őket szimulált Loxone-állapotokkal.
Nem készülékfotók és nem külön megrajzolt látványtervek.

## Eredet és formátum

A képek a `feature/loxone-home-controls` fejlesztés 2026-11-13-i helyi,
ASan/UBSan-műszerezett preview futásából származnak. A próba
`ZOLEE_CONTROLS_ONLY=1 ZOLEE_PREVIEW_RGB565=1` módban futott, az aktuális
v4 `loxone-home.zip` SD-erőforráscsomaggal. Az alap kártyaoldal mindkét
témáját, a dimmer, az időzítő, az összesítő és a motoros ablak részletes
kezelőjét mutatják.

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
    "controls-timer-day", "controls-monitor-day", "controls-window-day",
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
a cél és a visszajelzett érték különválasztását, az iránytiltást, a
256 sorig lapozható összesítő kezelőjét, az elavult ablakok elutasítását,
a régi ikoncsomag képhiányának kezelését és a változatlan állapotnál
elvárt nulla LVGL-invalidációt is ellenőrzi. A nagy lista korlátját a
modell korlátozza; a felületi fixture 14 soron ellenőrzi az első,
köztes és utolsó oldal működését.
