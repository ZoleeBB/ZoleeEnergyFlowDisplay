# Audio – dokumentációs képek

Ezek a 1280×800-as PNG-k a Master tényleges LVGL-megjelenítőjének helyi
előnézetéből készülnek, fixture állapotokkal. A nappali és éjszakai
lejátszóburkolat, illetve a borítót helyettesítő ikon az Otthon v4
SD-csomagból töltődik be.

A végleges RGB565 render PPM-kimenetének minden visszaolvasott RGB-képpontja
megegyezik az itt tárolt PNG-kével; a képek nem lettek átméretezve.

A 2026-11-14-i dokumentációs ellenőrzés az aktuális forrásból újrafordított,
ASan-műszerezett LVGL-előnézetet futtatott. Mindhárom meglévő PNG
képpontonként egyezett az új PPM-kimenettel, ezért a képfájlok változatlanok
maradtak. A kártyák, hangerőablak és részletes kezelő képeit vizuálisan is
ellenőriztük.

- `audio-cards-day.png`: Audio-kártyák nappali nézetben.
- `audio-volume-day.png`: kis hangerőablak nappali nézetben.
- `audio-detail-night.png`: részletes kezelő és kedvencek éjszakai nézetben.

A címek, hangerők és kedvencek tesztadatok. A képek nem élő Audioserver-
kapcsolatból és nem fizikai panelről származnak; az SD-ikon megjelenése
nem igazolja a távoli borítók letöltését. Nincs utólag rajzolt kezelő vagy
helyettesített képernyőrészlet.

[Használat, korlátok és fizikai elfogadás](../../audio-controller.md).
