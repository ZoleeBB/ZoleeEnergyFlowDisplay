# Külső források

## stb_image.h

- Upstream: [nothings/stb](https://github.com/nothings/stb)
- Verzió: **stb_image v2.32**
- Rögzített commit: `9600522de53973336fa033a289858a4354cbb04c`
- [Eredeti fejléc](https://github.com/nothings/stb/blob/9600522de53973336fa033a289858a4354cbb04c/stb_image.h)
- SHA-256: `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`
- Licenc: a fejléc végén található MIT licenc; az upstream teljes szövege megmaradt.

A fájl **változatlan upstream másolat**. Frissítésekor a forrást, a commitot
és az ellenőrző összeget együtt kell módosítani; kézi javítás nem kerül bele.

Az Audio borítódekódere kizárólag a JPEG-részét fordítja. A meglévő TJpgDec
marad az elsődleges út; a tartalék a nem támogatott színmintavételezésű és
progresszív, legfeljebb 512×512-es képeket kezeli. A normál TJpgDec-út
legfeljebb 1024×1024-es forrást fogad, hogy a 640×640-es Spotify-borítók is
megjelenjenek. A végeredmény továbbra is legfeljebb 256×256 RGB565.

Minden dekóderfoglalás PSRAM-ba kerül. A tartalékút méretkorlátja mellett
a komponens-, együttható-, sor- és RGB-pufferek együtt 4 MiB alatt maradnak;
normál JPEG esetén a korábbi 16 KiB-os TJpgDec-munkatér elegendő. A PNG
512×512-es korlátja, a 256 KiB-os tömörített bemenet és az 512 KiB-os
borítócache változatlan. A képfeldolgozást a már fenntartott OTA-futtató
egyképes callbackje végzi, külön média-task nélkül és a firmware-műveletek
elsőbbségével. A kaputelefon hardveres JPEG-erőforrását nem használja.

A wrapper `STB_IMAGE_STATIC` konfigurációt használ: a JPEG API privát,
és a fordító elhagyhatja a nem használt, nagy helyi puffert tartalmazó
függőleges tükrözést. A saját modul figyelmeztetései megmaradnak; a GCC
JPEG-only módban deklarált, de nem definiált static upstream API-jainak
késleltetett diagnosztikája fájlvégi kezelést kap. A valódi `.su` kapu a
megmaradó vendorfüggvényeket is ellenőrzi, általános kihagyás nélkül.
