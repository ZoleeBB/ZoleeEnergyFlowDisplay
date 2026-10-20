# Loxone Otthon – közös ikonkészlet

64 egységes, valósághű ikon a fogyasztói és a szabad Loxone-kártyákhoz.
Mindegyik különálló **96×96 px, RGBA, átlátszó PNG** az `icons/` mappában.
A teljes PNG-készlet 682 017 bájt; a nagy generált eredetik nincsenek a repóban.

## Katalógus

A `catalog.json` a szerzői igazságforrás. Egy tétel stabil `id` kulcsot,
magyar (`hu`) és angol (`en`) nevet, kategóriát és relatív `file` útvonalat ad.
A tárgyleírás, pontos prompt, generátor és SHA256 ellenőrzőösszegek az eredet
követését szolgálják. A képek a beépített `image_gen` eszközzel készültek.

A készlet tartalmazza a kért háztartási gépeket és klímaváltozatokat, továbbá
fagyasztót, kisgépeket, robotporszívót, számítógépet, NAS-t, routert, szivattyúkat,
érzékelőket, nyílászárókat, árnyékolókat, biztonsági és kerti eszközöket,
többféle lámpát és öt otthoni jelenetet.

## Ellenőrzés és bővítés

A repó gyökeréből, Pillow-val:

```sh
python3 tools/import_home_icons.py --complete
```

Az ellenőrzés hibát jelez hiányzó/felesleges fájlnál, hibás méretnél,
színmódnál, átlátszóságnál vagy SHA256-nál. A CI ugyanezt futtatja.

Új ikonhoz először a szerzői katalógust kell bővíteni, majd valódi átlátszó
RGBA képet generálni. A külső importjegyzék JSON-lista, tételei:

```json
[
  {"id": "uj-eszkoz", "path": "/abszolut/ut/eredeti.png", "prompt": "A pontos generálási leírás"}
]
```

```sh
python3 tools/import_home_icons.py /kulso/ut/import.json
python3 tools/import_home_icons.py --complete
```

Az import a kért méretre alakít, középre helyez és rögzíti az eredetet;
nem fest át és nem távolít el hátteret. Az eredeti megmarad a külső helyén.
Mindig a tényleges 96×96 változatot is ellenőrizni kell világos és sötét alapon.

## Integráció állapota

Ez az elkészült grafikai készlet, még nincs bekötve a webes ikonválasztóba,
az új SD-s speciális csomagba vagy a firmware UI-ba. A további feladatokat a
`docs/v3-development-plan.md` 6. checkpointja tartalmazza.

A későbbi SD-csomaghoz tömör runtime-katalógust kell előállítani a szerzői
promptok nélkül. Csak az aktív oldal látható ikonjait kell dekódolni:
egy 96×96 RGBA kép 36 KiB, 12 kép 432 KiB nyers képadat a dekóder és LVGL
járulékos memóriaigénye nélkül. A teljes készlet egyidejű betöltése nem szükséges.
Az inaktív fogyasztó szürke megjelenítését a UI-nak kell megoldania;
nincsenek külön, tárhelyet növelő szürke PNG-másolatok.
