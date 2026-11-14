# Képek és betűkészletek

A beépített nézet futásidejű PNG-ket használ a `main/assets/png` alatt. A forrásképek és a vágási/méretezési leírás az `assets/source` könyvtárban vannak; újragenerálás: `python tools/prepare_assets.py`. A közös összegző forráslap több ikonhoz szükséges, ezért megmarad.

A tizenegy SD-s sablon saját képei a `templates` könyvtárban találhatók. A jelenlegi fájlok SHA-256 ellenőrzőösszege és az ismert generálási eredet az [assetjegyzékben](../assets/template-manifest.json) szerepel. A korábbi képcserék elavult promptjegyzékei nem részei a repónak. A hiányzó eredeti generálási metaadatokat a jegyzék nem találja ki.

Átlátszó ikon importálása: `python tools/import_template_icons.py manifest.json`; a bemeneti lista elemei `template`, `name`, `path`, `prompt` mezőket tartalmaznak. A program az alfa megtartásával 128×128 PNG-t készít és aktualizálja az assetjegyzéket.

A beágyazott betűkészletek mind használatban vannak. A Noto Sans és DejaVu licenceket a betűforrások mellett meg kell tartani. A generált C fájlok a firmware fordítási bemenetei, nem ideiglenes állományok.

A webes sablonkezelő HTML/JS forrása és a belőle generált `main/app_template_page.h` együtt szükséges. Módosítás után az erre szolgáló generátort futtasd.

![Sablonok és időjárási ikonok](previews/template-icons.png)

## Loxone kapcsolat képernyő

A [helyben tárolt Loxone-logó](../assets/loxone-logo.png) a Loxone saját
LxCommunicator.NET MIT-licencű példatárának változatlan `Logo.png` képe.
A `main/app_loxone_logo.h` ugyanezt a PNG-t ágyazza a firmware-be, ezért
a kijelzőnek nincs szüksége internetelérésre vagy SD-s logófájlra.
A helyi PNG az USB-telepítőcsomag `assets/loxone-logo.png` fájlja is. A márkajelzés kizárólag
az adatkapcsolat megjelölésére szolgál. A Loxone név és logó a jogosult tulajdona.
A példatár licencét a `main/licenses/Loxone-LxCommunicator-MIT.txt` őrzi.

## Páratartalomikon

Az új, átlátszó generált kék csepp a `assets/source/weather_humidity.png`
eredetiből készül: `python3 tools/prepare_humidity_icon.py`. A beépített és 4B
nézet 28×28-as, a hét érintett SD-téma 30×30-as natív PNG-t használ,
3136/3600 bájtos ARGB8888 képpontadattal. Az új fájlok 2 KiB alatt maradnak.
A beépített változat eredetét az `assets/source/weather_humidity.json`, az
SD-változatokat az assetjegyzék tartalmazza. [Működés](relative-humidity.md).

## IRC termosztátikon

A kötött digitális termosztátgrafika méretezett forrása az
`assets/source/irc-thermostat.png`: **384×351 RGBA PNG, 89 289 bájt**.
A verziókezelt forrás a natív ikon négyszeres szélességét őrzi; a nagy
generálási eredeti nem része a repónak. A `tools/prepare_irc_icon.py`
arányosan kicsinyíti és átlátszó vászonra, középre helyezi:

```sh
python3 tools/prepare_irc_icon.py
```

A kimenet az `assets/loxone-home/icons/thermostat-irc.png`,
**96×96 RGBA PNG, 8048 bájt**. A kép kijelzője üres marad; a célhőmérsékletet
és a kis fűtési/hűtési üzemmódjelet a firmware LVGL-kódja rajzolja rá.
A méretezett forrás és a generátor a kész ikont reprodukálhatóan őrzi;
a kész PNG-t nem kell kézzel módosítani. Ezek a fejlesztői fájlok a repóban
találhatók, az offline USB-telepítőnek nem részei.

Az ikon a közös **v4 SD-csomagba** kerül, stabil azonosítója `thermostat-irc`.
A firmware nem tartalmaz beágyazott példányt. A webes előnézet útvonala
`/api/templates/home-icon?id=thermostat-irc`; a kiszolgáláshoz telepített
Otthon-csomag kell. Régi csomagnál a termosztát tiszta szöveges kijelzéssel és
működő −/+ gombokkal jelenik meg, eltérő geometriájú pótkép nélkül.
Az aktív oldal IRC-kártyái egy közös, 36 864 bájtos ARGB8888-képpontpuffert használnak.
Ez a kép tartós képpontadata; a dekóder átmeneti foglalásait és a UI-objektumokat
nem tartalmazza. [Használat és memóriakezelés](irc-controller.md).

## Redőnyikon

A rögzített redőnygrafika az
`assets/loxone-home/icons/blinds-shading.png` fájl: **96×96 RGBA PNG, 11 648 bájt**.
A natív méretű, átlátszó kép kerül a közös SD-csomagba;
nincs új nagy felbontású forráskép vagy további átméretezési lépés:

```sh
python3 tools/prepare_shading_icon.py
```

A generátor a korábbi általános `assets/loxone-home/icons/blinds.png` képet
méret- és formátumellenőrzés után másolja a kötött `blinds-shading.png` névre,
így az alfa és az eredeti PNG
bájtjai megmaradnak. Ezek a fejlesztői fájlok a repóban találhatók;
az offline USB-telepítő dokumentációja kódútvonalként hivatkozik rájuk.

A kötött azonosító **`blinds-shading`**. A webes előnézet útvonala
`/api/templates/home-icon?id=blinds-shading`, amely webes hitelesítés után a
telepített SD-csomagból olvas. A v4-csomag 79 ikont tartalmaz; az eredeti
`blinds` továbbra is választható általános ikon. Régi csomag betöltésekor az
azonos képtartalmú `blinds` használható a redőnykártyához. Az aktív oldal összes
redőnykártyája egyetlen,
**36 864 bájtos ARGB8888-képpontpuffert** használ. Ez a tartós képpontadat
mérete, a dekóder és a UI további foglalásai nélkül.
[Használat és memóriakezelés](shading-controller.md).


## Új Loxone-kártyák és Audio-burkolatok

A 13 új funkcióikon a meglévő háztartási készlettel egyező, átlátszó 96×96-os
RGBA PNG. A kapuk mind a hat Loxone-típusához önálló kép tartozik. A megjelenítési
nevek csoportprefixet kapnak, a korábbi ikonazonosítók és mentett párosítások
megmaradnak. A generálási promptot, forrás- és runtime SHA-256 értéket az
`assets/loxone-home/catalog.json` rögzíti; importálás és ellenőrzés:

```sh
python3 tools/import_home_icons.py --complete
python3 tools/prepare_home_resources.py
```

Az Audio-kártya két 292×200-as RGB PNG-burkolata az
`assets/loxone-home/skins` alatt található. A runtime csak az aktív nappali vagy
éjszakai változatot tölti be, egyetlen közös RGB565-pufferrel. A lemezborító,
a zónanév és a vezérlőjelek élő UI-elemek, nem részei a burkolat képének.
Az internetes lemezborítók a korlátozott PSRAM-médiatárban élnek; nem kerülnek
bele a firmware-be és nem íródnak minden számváltáskor az SD-re.
