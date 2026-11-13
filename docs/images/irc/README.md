# IRC-kártyák dokumentációs képei

[Használati útmutató és képgaléria](../../irc-controller.md#statikus-előnézetek)

Az `irc-cards-day.jpg`, `irc-cards-night.jpg` és `irc-cards-twelve.jpg`
960×600 képpontos képek a firmware valódi LVGL-felületének 1280×800-as
natív tesztrenderéből készültek, LVGL v9.5.0-val. A próba forrása a repóban
a `tests/preview/irc_test.inc`, amely a tényleges kártyakódot szimulált
Loxone-állapotokkal futtatja.
A képek nem fizikai panel fotói vagy külön megrajzolt látványtervek.

A `tests/preview` lefordított `ui_preview` célján a `ZOLEE_IRC_ONLY=1`
kapcsoló futtatja az IRC-próbát. A `ZOLEE_PREVIEW_RGB565=1` a panelnek
megfelelő RGB565 renderutat választja. A dokumentáció az RGB565 futás
PPM-kimeneteiből készül: Pillow `thumbnail((960, 600), Image.Resampling.LANCZOS)`,
majd JPEG-mentés `quality=88`, `subsampling=0`, `optimize=True` beállításokkal.
A JPEG-előnézetek tömörítése veszteséges; a teljes felbontású PPM-ek helyi
tesztkimenetek, nem kerülnek a repóba. Frissítéskor a 960×600-as méretet
és a használati útmutató hivatkozásait meg kell tartani.

Ezek a JPEG-képek verziókezelt dokumentációs fájlok. Nem kerülnek a firmware
futásidejű erőforrásai vagy az SD-s Otthon-csomag ikonjai közé.
A méretezett termosztátgrafika és a natív 96×96-os ikon külön eredetleírása:
[IRC termosztátikon](../../assets.md#irc-termosztátikon).
