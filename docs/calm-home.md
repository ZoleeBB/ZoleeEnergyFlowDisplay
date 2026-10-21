# Nyugodt otthon – 05-calm-home v9

A felhasználó 2026-10-21-én elfogadta a meleg nappalibelső, három nagy energiaadat
és visszafogott napi összesítők irányát, majd a külön generált, zsályazöld kanapés,
kertre nyíló hátteret. Ez a fotó kizárólag e témához készült; nem az Otthon vagy
a ház fogyasztói oldalának, illetve más energiakártyának a háttere.
Az azonosító és a témák sorrendje változatlan, a sablon verziója 9.

- Nappal: világos, meleg színű panelek és sötét feliratok.
- Éjjel: azonos szoba és elrendezés, meleg lámpafény, sötét panelek.
- Főadatok: napelem, házfogyasztás, akku töltöttsége. Mind élő LVGL-felirat.
- További élő értékek: hálózati irány/teljesítmény, időjárás, hőmérséklet, szél,
  óra/dátum, rendszerállapot, napi termelés/fogyasztás/visszatáplálás.
- Külön asszisztenshely: x330/y118, 654×68. Nem fed fontos adatot vagy érintőcélt.
- Külön Otthon érintőgomb: x1130/y450, 118×140. A meglévő Loxone Master
  oldalt nyitja, a meglévő mód- és jogosultsági szabályokkal.
- Valós HU/EN feliratok; a beépített Rendszer téma nem változik.

## Grafika és méret

A grafika a beépített imagegen eszközzel készült. A nappali háttér új generálás,
az éjszakai az elfogadott nappali kép megvilágításának célzott szerkesztése.
A nyers, nagy generált képek nem részei a repónak vagy a telepítőcsomagnak.

| Fájl | Méret | Bájt |
| --- | --- | ---: |
| `templates/05-calm-home/day.png` | 1280×800, 8 bites RGB | 871224 |
| `templates/05-calm-home/night.png` | 1280×800, 8 bites RGB | 720798 |
| `preview-light.png`, `preview-dark.png` | 480×300 | tényleges LVGL-render |

Előkészítés: Pillow Lanczos méretezés 1280×800-ra, majd a kijelző RGB565
formátumában elvesző alsó csatornabitek nullázása (R/B `& 248`, G `& 252`).
Ez nem általánosan veszteségmentes RGB888-átalakítás, de az eredeti méretezett
kép és az új fájl **azonos RGB565-színértékeket** ad; ez minden pixelre ellenőrzött.
Az ezt követő oxipng-tömörítés veszteségmentes. A fájl 8 bites RGB marad,
nem indexelt PNG; a meglévő firmware-validátor és dekóder kezeli.
Nem változik az általános képlimit, a képcache vagy a firmware-erőforráskeret.
A két háttér összesen 8192000 bájt konzervatív RGBA-dekódolási keretet jelent;
a teljes téma 8847360 bájt (8,44 MiB), a meglévő 12 MiB limit alatt marad.
A teljes telepítő ZIP mérete 2139830 bájt. A képek SD-s sablonadatok,
nem új firmware-be ágyazott képek. Fizikai paneles memória/FPS-próba hátravan.

A manifest a tényleges, kicsinyített fájlok SHA256-át tartalmazza. Az előnézetek
újragenerálása más témák és a beépített firmware-előnézetek átírása nélkül:

```sh
python3 tools/prepare_template_previews.py /path/to/lvgl-renders --template 05-calm-home
python3 tools/prepare_templates.py
python3 tests/test_template_packages.py
```

## Generálási specifikáció

A beépített imagegen eszköznek adott nappali specifikáció: új, eredeti, nyugodt,
fotórealisztikus nappalibelső; 8:5 tájkép, célméret 1280×800; átlós saroknézet,
balra zsályazöld kanapé krémszínű takaróval, lekerekített falfülke könyvekkel és
kerámiákkal, középen világos tölgy ovális asztal, jobbra tölgykeretes üvegajtó
csendes zöld udvarra. Meleg vakolat, tölgy padló és visszafogott szőnyeg,
jobbról természetes délelőtti fény, nyugodt alsó és felső terület. Kizárólag háttér:
se szöveg, se ikon, kártya, adat, logó, ember vagy vízjel. Új szoba és kameranézet,
a korábbi frontális krémszínű kanapés kép elrendezésének újrahasználata nélkül.

Az éjszakai szerkesztés specifikációja: a nappali kép kamerája, képkivágása,
építészete, összes bútora és tárgypozíciója maradjon változatlan. Csak a napszak
és a világítás változzon: kint sötétkék éjszaka látható növényzettel, napfényfoltok
nélkül; a meglévő állólámpa világítson meleg borostyán fénnyel. Halk beltéri
alapfény, meleg belső és hideg külső tónus; ne kerüljön új tárgy, lámpa, hold,
felirat vagy felületelem a képre. Azonos 8:5 képkivágás, célméret 1280×800.

## Ellenőrzés

A tényleges natív LVGL-szimulátor mindkét napszakot, HU/EN szövegeket,
15 kW-os értékeket, 100%-os SOC-ot és a hosszabb felkészülési állapotfeliratot
ellenőrzi. A teljes kiírt szöveg méretét a valós LVGL-fonttal hasonlítja a
szövegmezőhöz; a főadatok a tényleges élő formázás eredményével egyeznek.
A közös asszisztenspróba ellenőrzi az átfedésmentességet. A teljes natív
regresszió, a ZIP-telepítő és a csomagellenőrzés is része az ellenőrzésnek.
A szimulátorképek szintetikus adatokat mutatnak, nem fizikai paneles tesztet.

A 06-energy-orbit áttervezése továbbra is külön látványterv-jóváhagyást igényel.

A forráscommit (`1c6314a9b2edb4054d08e894bbf89286ad81b1bb`) minden GitHub
ellenőrzése sikeres, a két ESP-IDF firmware és a normál/kézi boot USB dry-run is.
Mindkét firmware mérete a 14. checkpoint méretével egyezik; ez nem bájtraazonossági
állítás. Pontos futások, méretek és hash-ek: a fejlesztési terv 15. checkpointja.
