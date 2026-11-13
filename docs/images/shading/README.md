# Redőnykártyák dokumentációs képei

[Használati útmutató és képgaléria](../../shading-controller.md#statikus-előnézetek)

A `shading-cards-day.jpg`, `shading-cards-night.jpg`,
`shading-cards-twelve.jpg` és `shading-position-day.jpg` a firmware
tényleges LVGL-felületének natív tesztrenderéből készült,
**960×600 képpontos JPEG-előnézetek**. A próba forrása a repóban a
`tests/preview/shading_test.inc`, amely a valódi kártya- és felugróablak-kódot
szimulált Loxone-állapotokkal futtatja. A render nem fizikai panel fotója.

A nappali/éjszakai kép hat redőny-, egy IRC- és egy világításkártyát,
a teljes oldal tizenkét redőnykártyát, a negyedik kép az egyedi
százalékos beállítóablakot mutatja. A négy JPEG összesen **710 250 bájt**;
egyik fájl sem éri el a 200 KiB-ot.

A `tests/preview` lefordított `ui_preview` célján a `ZOLEE_SHADING_ONLY=1`
kapcsoló indítja a redőnypróbát; a `ZOLEE_PREVIEW_RGB565=1` a panelnek
megfelelő RGB565 renderutat választja. A dokumentációs képek ebből a
1280×800-as PPM-kimenetből készülnek: Pillow
`thumbnail((960, 600), Image.Resampling.LANCZOS)`, majd JPEG-mentés
`subsampling=0`, `optimize=True` beállításokkal. A nappali vegyes oldal
(`shading-cards-day.jpg`) minősége `quality=86`, a másik három képé
`quality=88`, hogy minden fájl 200 KiB alatt maradjon.

A teljes méretű PPM-ek és a teszt köztes kimenetei helyi állományok,
nem kerülnek a repóba. A verziókezelt JPEG-eket frissítéskor is 960×600-as
méretben kell tartani, és ellenőrizni kell a használati útmutató
képhivatkozásait. A dokumentációs képek nem firmware-erőforrások és nem
részei az SD-s Otthon-ikoncsomagnak.

A kártya rögzített, natív 96×96-os ikonja a már meglévő redőnykép
bájtazonos másolata. Eredete és ellenőrzött generálása:
[Redőnyikon](../../assets.md#redőnyikon).
