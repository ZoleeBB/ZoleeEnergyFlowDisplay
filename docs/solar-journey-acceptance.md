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
