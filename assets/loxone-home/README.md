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

## Integráció és SD-csomag

A `/loxone` webes szerkesztő 64 ikont kínál. A firmware-be csak a tömör id/hu/en
lista kerül; a generálási leírások nem. Az ikonok, a `backgrounds/` két pontos
1280×800 képe és a meglévő helyi logó külön, nem lapozható SD-erőforráscsomagot alkot:

```sh
python3 tools/prepare_home_resources.py
```

A `dist/templates/loxone-home.zip` a meglévő témakezelővel telepíthető és frissíthető.
A normál témák 32 PNG-s korlátja nem változik. A fenntartott közös csomag kizárólag
64 egyedi ikont, két 1280×800 hátteret, kis logót és opcionális előnézeti párt fogad.
A runtime csak a megnyitott oldal legfeljebb 12 ikonját, egy hátterét és a logót
olvassa elő; azonos ikon csak egyszer kerül memóriába. Bezáráskor ezek és a szürke
képpéldányok felszabadulnak, az előző energiaelrendezés visszatöltődik.

A `backgrounds/light.png` a jóváhagyott nappali szoba kártyák/feliratok nélküli
rekonstrukciója, a `dark.png` ugyanennek az éjszakai változata. A beépített image_gen
eszközzel készültek; a nagy eredetik nincsenek a repóban. Végső méretük 1280×800 RGB.
A PNG-k a panel RGB565 színpontosságára vannak optimalizálva: a vörös/kék csatorna
alsó 3, a zöld alsó 2 bitje nulla. Ez nem változtatja meg a kijelző RGB565
színértékeit, de a két háttér együtt 3 612 352 helyett 1 946 416 bájt.
A fájlok továbbra is valódi RGB PNG-k; palettás PNG-t a csomagellenőrző nem fogad.
A nappali prompt a referenciából minden UI-elem eltávolítását és a nyugodt,
nagy ablakos, zöld környezetű nappali/konyha megtartását kérte. Az éjszakai prompt
kizárólag időjárás-/fényváltást kért, változatlan kamerával és berendezéssel,
sötétkék kerttel és a meglévő meleg beltéri világítással. Az éjszakai oldal végső
vizuális elfogadása a fejlesztési tervben követendő.

A fogyasztó 0 mérésénél valódi, luminanciaalapú szürke ARGB-változat készül;
a színes ikon nem kap egyszerű áttetsző fedőréteget. Az alfa megmarad.

A teljes folyamat ellenőrzése:

```sh
python3 tests/test_home_resources.py
python3 tests/test_loxone_home.py
```
