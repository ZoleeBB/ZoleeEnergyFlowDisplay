# Nap pálya – követelmény-ellenőrzés (2026-10-15)

## Ellenőrzött, kódoldalon megvalósított részek
- Opciós `SunPosition` adat a Loxone MQTT/HTTP mintákban és a firmware-ben, T1 szöveges bemenettel.
- Napkelte/napnyugta, napmagasság és irány, nappali és éjszakai ív, dinamikus napkorong.
- Teljesítményfüggő napkorong-fényudvar 8 kW-nál telítődve.
- Órás, 24 oszlopos termelésdiagram: helyi/tárolt zöld és export sárga.
- Mini energiaáramlás, akkumulátor-hőmérséklet, opcionális kapacitás.
- Átrendezhető témakatalógus, Rendszer téma rögzített első helyen.
- Grafikonkitöltés színkövetése és 7 kW-ig teljesítményfüggő energiapötty-sebesség.

## Nem kész / igazolást igényel
- A GitHubban a `templates/09-solar-journey/template.json` még v1; a külső ZIP-ben v2 van. **Nem ekvivalens a kettő.**
- A 13 újragenerált PNG a `NapPalya-komplett-sablon.zip` állományban elérhető, de nincs mindegyik a GitHub forrásába commitolva.
- A GitHubban a témában még régi háttérképek és egyszerű energiaikon/helyettesítők szerepelnek.
- A `SunPosition = null` esetén a napkomponensek eltűnnek, de az egész téma nem kerül automatikusan ki a lapozható listából – ez még külön implementálandó.
- A napkorong tényleges képi megjelenése jelenleg LVGL-kör; a generált napgrafika nincs a képernyőre kötve.
- A jóváhagyott látványtervvel pixel- és elrendezés-szintű egyezés, teljes CI, ESP32-P4 eszközös teljesítménypróba még nincs igazolva.

## Kiadási feltétel
Ne legyen végleges merge, amíg a ZIP-ben lévő v2 grafika és JSON a forrásba nem kerül, a webes telepítés és az SD-kártyás betöltés tesztelve nincs, és az új témát a kijelzőn vizuálisan nem hagytuk jóvá.

## 2026-10-15: bináris csomag feltöltve

A Nap pálya grafikai csomag immár mind a 13 PNG-vel együtt szerepel a `feature/solar-journey-template` ágon. A firmware által engedélyezett betűméretek: 12, 16, 20, 28, 48, 96; a feltöltött v2 elutasítását 34 szabálytalan méret okozta. A v3 sablon mindkét jelenetet kizárólag engedélyezett betűméretekkel írja le. A teljes integráció még csak sikeres CI-futás után tekinthető ellenőrzöttnek.

## 2026-10-15: fejlesztési javítócsomag

- Nap pálya sablon v4: az összes 13 grafikai PNG használatban van; a napkorong különálló PNG; a szél piktogramja külön elem.
- A három mini energiaút `glow` értéke rögzítve, ezért az LVGL fényudvar-objektum ténylegesen létrejön, az előnézeti tesztben is.
- Hálózat importpiros, exportcitromsárga.
- A lapozó a Nap pálya témát átugorja, ha `SunPosition` nem érvényes. Már korábban kiválasztott téma esetén a napívek és a napadatok továbbra is rejtettek maradnak.
- Valódi kijelzőn mért FPS, heap-csúcs, renderidő még nem mérhető CI-ben; nem állítható, hogy a hardveres validáció megtörtént.
