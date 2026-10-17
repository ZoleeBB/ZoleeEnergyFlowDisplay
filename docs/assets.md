# Képek és betűkészletek

A beépített nézet 30 futásidejű PNG-t használ a `main/assets/png` alatt. A forrásképek és a vágási/méretezési leírás az `assets/source` könyvtárban vannak; újragenerálás: `python tools/prepare_assets.py`. A közös összegző forráslap több ikonhoz szükséges, ezért megmarad.

A kilenc SD-s sablon saját képei a `templates` könyvtárban találhatók. A jelenlegi fájlok SHA-256 ellenőrzőösszege és az ismert generálási eredet az [assetjegyzékben](../assets/template-manifest.json) szerepel. A korábbi képcserék elavult promptjegyzékei nem részei a repónak. A hiányzó eredeti generálási metaadatokat a jegyzék nem találja ki.

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
