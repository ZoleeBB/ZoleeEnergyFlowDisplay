# Grafikai assetek

A dashboard felismerhető piktogramjai **nem egyszerű LVGL primitívekből készülnek**. A jóváhagyott eredeti látványtervhez igazodó, részletes grafikai asseteket használunk.

## PNG irány

A normál dashboard fő grafikái 128×128-as, átlátszó hátterű PNG fájlokként kerültek a repository-ba:

`main/assets/png/`

Az elsődleges készlet:

- `header_home_leaf.png`
- `metric_leaf.png`
- `metric_house.png`
- `metric_grid.png`
- `metric_coins.png`
- `flow_battery.png`
- `flow_grid.png`
- `flow_house.png`
- `flow_hub.png`
- `flow_solar.png`

Ezek közül több közvetlenül a jóváhagyott mockup grafikai részleteiből készült, ezért a színezés, a puha körháttér, az árnyékolás és a részletezettség lényegesen közelebb áll a látványtervhez, mint a korábbi kézzel felépített SVG-k.

A PNG-k jelenleg **a firmware-be vannak ágyazva**, nem az SD-kártyáról töltődnek. A teljes képanyag kicsi a 32 MB flashhez és a két 8 MB-os OTA partícióhoz képest, ezért nincs indok az SD-kártya elérhetőségétől függővé tenni a normál felületet.

Az LVGL 9.5 LodePNG dekódere végzi a megjelenítést.

## Átmeneti SVG-k

A ritkábban megjelenő időjárási, kommunikációs és speciális státuszállapotok migrációja folyamatban van. Ezek addig ugyanazon `app_assets.c` rétegen keresztül használhatók, ezért a UI kódot a PNG-re váltáskor nem kell újratervezni.

Az SVG források az `assets/svg/` könyvtárban referenciaként megmaradhatnak.

## Dinamikus elemek

Az alábbi elemek továbbra is LVGL primitívek, mert valós időben változnak, és nem statikus ikonok:

- energiaáramlási összekötő vonalak;
- mozgó energia-pontok;
- grafikonok;
- kártyák és keretek;
- akkumulátor SOC sáv;
- aktuális idővonal a grafikonon.

A cél az, hogy **minden felismerhető ikon és illusztráció asset-alapú legyen**, miközben a dinamikus adatvizualizáció natív LVGL marad.
