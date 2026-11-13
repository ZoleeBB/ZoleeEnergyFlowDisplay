# Világításvezérlés – statikus dokumentációs képek

Ezek a képek a [világításvezérlő útmutató](../../lighting-controller.md)
és a [projekt README](../../../README.md) előnézetei. Nem futásidejű
képerőforrások: a dokumentáció alatt maradnak, nem a `main/assets` vagy
az SD-s `templates` könyvtárban.

## Eredet

A 2026-11-13-i Actions #168, `ui-preview` job öt eredeti PPM-fájlja:
`lighting-cards-day`, `lighting-circuits-day`, `lighting-color-day`,
`lighting-color-night`, `lighting-white-day`.
A forráscommit `d965a6b3905ce91eeb76d74c5c315e63c622b029`,
a ténylegesen renderelt PR-merge commit
`bbce0502f98cd6bd822ea160d834cdcf85aaf127`.

A render a firmware valódi LVGL-kódját futtatja, szintetikus Loxone-adatokkal,
`ZOLEE_LIGHTING_ONLY=1 ZOLEE_PREVIEW_RGB565=1` módban. Nem készülékfotó
és nem újrarajzolt látványterv. Az eredeti 1280×800 felbontás megmaradt;
a PPM → PNG átalakítás veszteségmentes, a dekódolt RGB-képpontok egyezését
mind az öt fájlnál ellenőriztük.

Az eredeti, archiválási forrásként használt artifact azonosítója
`14021210725`; ZIP SHA-256:
`aefde4d91037b87c043db1ac8a0760d2a899670b85a97991f747f3c09a7ba42d`.
A dokumentáció megnyitásához nincs szükség erre az artifactra vagy
GitHub Actions-hozzáférésre: minden kép helyi, relatív PNG-hivatkozást kapott.

## Frissítés felületi változás után

A `.github/workflows/ci-shared.yml` `ui-preview` jobjában szereplő
build-/erőforrás-előkészítés és világítási renderpróbák továbbra is futnak.
Külön előnézeti artifact feltöltése nincs. A már létrejött
`.preview-images/lighting-*.ppm` fájlokat a repó gyökeréből így lehet
veszteségmentesen átalakítani (Pillow szükséges):

```python
from pathlib import Path
from PIL import Image

names = (
    "lighting-cards-day", "lighting-circuits-day", "lighting-color-day",
    "lighting-color-night", "lighting-white-day",
)
output = Path("docs/images/lighting")
output.mkdir(parents=True, exist_ok=True)
for name in names:
    with Image.open(Path(".preview-images") / f"{name}.ppm") as source:
        image = source.convert("RGB")
        assert image.size == (1280, 800)
        target = output / f"{name}.png"
        image.save(target, format="PNG", optimize=True, compress_level=9)
        with Image.open(target) as saved:
            assert saved.convert("RGB").tobytes() == image.tobytes()
```

Frissítéskor vizuálisan is nézd át mind az öt képet, frissítsd az itteni
forrásazonosítókat, és futtasd a `python3 tests/test_workflows.py` ellenőrzést.
A képek csak tudatos dokumentációfrissítéssel változnak; a CI nem commitolja
vissza a renderelés eredményét és nem ír a fejlesztési ágra.
