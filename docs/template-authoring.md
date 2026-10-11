# Saját sablon készítése

A sablon egy önálló SD-csomag: `template.json` és a hozzá tartozó saját PNG-k.
Nem kell új firmware-t fordítani az elrendezéshez. A rajzoló ugyanaz a fizikai
kijelzőn, a Windows-szimulátorban és az LVGL-előnézeti tesztben.
A teljes mezőlista és a telepítés leírása: [templates.md](templates.md).

## 1. Azonosító és munkakönyvtár

Hozz létre például egy `templates/08-my-home` könyvtárat. Az azonosító legfeljebb
32 kisbetűből, számból, kötőjelből és aláhúzásból állhat. A `builtin` foglalt.
A könyvtár neve és a JSON `id` mezője egyezzen. A `name` megjelenített név;
`version` pozitív egész, nem firmware-verzió. Meglévő webes csomagot előbb
el kell távolítani, majd az új ZIP-et feltölteni; a telepítő nem írja felül.

## 2. Saját vizuális világ és ikonok

Határozd meg a háttér, kártyák, betűk és ikonok közös stílusát. A hét mellékelt
sablon hét külön ikonkészletet használ. Az alapnézet PNG-it ne másold át új
sablonba. Részletes, kis méretben is felismerhető, keret nélküli képeket készíts;
a színes energiaág körvonala a rajzoló feladata, nem az ikon része.

- Ikonokhoz valódi alfaátlátszóságú, 8 bites RGBA PNG ajánlott, 128×128 px.
- A háttér lehet RGB/RGBA, legfeljebb 1280×800 px; ne süsd bele az élő
  adatokat, mozgó pontokat, diagramokat vagy szövegeket.
- Kisbetűs fájlnevek, alkönyvtár nélkül: például `solar.png`, `day.png`.
- Az `image` elem a megadott `w`/`h` méretre skáláz; azonos oldalaránnyal
  dolgozz, különben torzul. Világos és sötét háttéren is ellenőrizd az éleket.
- Csomagonként maximum 32 PNG, egyenként 2 MiB, összesen 4 MiB tömörített
  és 12 MiB dekódolt (`szélesség × magasság × 4`) memória használható.
  PNG interlace/palettás formátum nem támogatott; a CRC és a tömörített
  képadat ellenőrzésen megy át.

| Sablon | Saját ikonstílus |
|---|---|
| Élő otthon | Természetes anyagú, építészeti miniatűrök |
| Energiautak | Jáde és matt üveg, fényes belső részletek |
| Műszerfal | Szálcsiszolt fém, sötét műszaki zománc |
| Napi mérleg | Rétegelt, finoman domborított papír |
| Nyugodt otthon | Matt kerámia, krém és zsálya színek |
| Energiakör | Fazettás kristály, türkiz és ibolya fény |
| Energiafőkönyv | Réz és elefántcsont dombormű, gravírozott részletek |

A generált készletek stílusai és pontos promptjai:
[template-icon-prompts.json](template-icon-prompts.json). A forrásképekből
az alfa megtartásával készültek a csomagok 128×128-as runtime képei.
A `prepare_templates.py` saját ikon hiányában hibát jelez, és soha nem
helyettesíti az alapnézet ikonjával.

## 3. Két téma és rétegsorrend

Minimális működő `template.json`:

```json
{
  "schema": 1, "id": "08-my-home", "name": "Saját otthon", "version": 1,
  "light": {"background": "#F0F5F3", "elements": [
    {"type":"image","file":"solar.png","x":40,"y":130,"w":128,"h":128},
    {"type":"label","text":"Termelés","text_en":"Production","x":200,"y":130,"w":300,"h":30,"font":20,"color":"#19352F"},
    {"type":"value","source":"pv","x":200,"y":180,"w":300,"h":60,"font":48,"color":"#36AE7C"}
  ]},
  "dark": {"background": "#111F2C", "elements": [
    {"type":"image","file":"solar.png","x":40,"y":130,"w":128,"h":128},
    {"type":"label","text":"Termelés","text_en":"Production","x":200,"y":130,"w":300,"h":30,"font":20,"color":"#EDF4F8"},
    {"type":"value","source":"pv","x":200,"y":180,"w":300,"h":60,"font":48,"color":"#36AE7C"}
  ]}
}
```

A koordináták 1280×800-as képernyőre vonatkoznak. Az elemek a tömb sorrendjében
kerülnek egymás fölé: háttér → energiautak → ikonok/kártyák → értékek.
Az alsó 28 pixelt hagyd szabadon a lapjelzőnek és az SD-hibajelzésnek.
Maximum 96 elem lehet témánként; a JSON maximum 64 KiB. Az elem teljes
`x,y,w,h` téglalapja maradjon a képernyőn belül. A magyar szövegekhez UTF-8 kell.
A 48/96 pontos betűkészlet számjegyeket és mértékegységeket tartalmaz;
szöveges címhez 12/16/20/28 méret használható.

A `NightMode` választ témát, a lapozás választ sablont. A hibaképernyők közösek.
Az adatokat a `source` köti a meglévő energiacsomaghoz/statisztikához: például
`pv`, `home`, `grid`, `battery`, `soc`, `production_today`, `consumption_today`.
A napi érték kWh, a pillanatnyi teljesítmény kW; a megjelenítő formázza az egységet.

Szövegárnyékhoz helyezz a fő `label` vagy `value` elé egy ugyanolyan tartalmú,
2 px-rel eltolt fekete elemet `shadow: true` mezővel. A dinamikus értéknél
ugyanaz legyen a `source`. Így a fekete árnyék a hőmérséklet figyelmeztető
színezésénél is fekete marad. A `shadow` csak logikai érték lehet, és csak
`label` vagy `value` elemen használható; mindkét réteg beleszámít a 96 elembe.

## 4. Valódi görbe energiaút

```json
{
  "type":"flow", "source":"grid", "x":100,"y":300,"w":551,"h":241,
  "color":"#E66B6B", "radius":5, "curve":"bezier",
  "points":[{"x":0,"y":0},{"x":230,"y":0},{"x":230,"y":240},{"x":550,"y":240}]
}
```

A pontok a flow elem bal felső sarkához képest relatívak; az összes vezérlőpont
férjen a `w`/`h` határba. A `bezier` négy pontja: kezdőpont, első érintőpont,
második érintőpont, végpont. Hét ponttal két összefűzött köbös szakasz írható
le: `P0,C1,C2,P1,C3,C4,P2`. Folytonos érintőhöz a `C2,P1,C3` legyen egy egyenesen.
A `curve` elhagyása vagy `polyline` 2–8 pontos törött vonalat jelent.

A rajzoló szakaszonként 32 lépéssel mintavételezi a görbét. A vonal és a
négy mozgó pont ugyanazt a mintavételezett pályát használja; a pöttyök
úthossz szerint haladnak, így nem gyorsulnak fel az ív egyik részén.
Pozitív `grid`: hálózat → ház, negatív: visszatáplálás. Pozitív `battery`:
akku → ház, negatív: töltés. Ennek megfelelően add meg a kezdő- és végpontot.
Nulla teljesítménynél a pontok eltűnnek; a sebesség 7 kW-nál éri el a maximumot.
Az útvonal nem színeződik át irányváltáskor.

## 5. Saját időjáráskészlet

```json
{
  "type":"weather","x":40,"y":400,"w":128,"h":128,
  "files": {
    "sunny":"weather_sunny.png", "partly_cloudy":"weather_partly_cloudy.png",
    "cloudy":"weather_cloudy.png", "rain":"weather_rain.png",
    "snow":"weather_snow.png", "night":"weather_night.png"
  }
}
```

Mind a hat kulcs és fájl kötelező, ha `files` szerepel. A kiválasztás a közös
időjáráslogikát használja: csapadék elsőbbséget kap (−4 °C alatt hó), majd
NightMode, napsugárzás, termelés alapján részben felhős, végül felhős.
A korábbi, `files` nélküli külső sablonok továbbra is működnek a firmware
ikonjaival; új sablonhoz saját készletet adj meg.

## 6. Ellenőrzés, ZIP és telepítés

Az alábbiakat a repository gyökeréből futtasd:

```sh
python3 tests/test_templates.py
python3 tests/run_tests.py
```

Az első parancs minden `templates/` alatti csomagot a tényleges C-validátorral
ellenőriz, és sérült/veszélyes ZIP-eket is tesztel. Új, kézzel írt csomag ZIP-je:

```sh
python3 -m zipfile -c my-home.zip templates/08-my-home/template.json templates/08-my-home/solar.png
```

Ez a parancs a fájlneveket a ZIP gyökerébe teszi. Ne a teljes könyvtárat add
át neki. A mellékelt hét sablon JSON-ját és ZIP-jeit a
`python3 tools/prepare_templates.py` állítja elő. Új generált sablonhoz a
builder függvényt és a `main()` katalógusát is bővítsd; kézzel szerkesztett,
nyolcadik csomagot ez a script nem ír át.

A Windows-szimulátorban az SD-csomagokat a futtatható program melletti
`templates/` könyvtárba másold, majd indítsd újra. Bal/jobb nyíllal lapozz,
D-vel demóállapotot válts. Vizsgáld meg mindkét témát, hosszú HU/EN címkéket,
importot/exportot, töltést/leadást, nullát, időjárást és hiba utáni visszatérést.
Az automatikus LVGL-teszthez lásd `.github/workflows/build.yml` `ui-preview`
jobját; a `tests/preview/preview.c` csomagszámát új referencia-sablon hozzáadásakor
frissítsd. A renderelt kép a valódi widgetekből készül.

Végül töltsd fel a ZIP-et az eszköz `/templates` oldalán, és külön aktiváld,
vagy másold a könyvtárat az SD `/templates` mappájába, majd indítsd újra.
A webes kezelés és a memóriakorlátok részletei: [templates.md](templates.md).

## Ellenőrzött referencia

A hét csomag 14 témáját, mind a nyolc nézeten át vezető érintési lapozást,
a közös hiba utáni visszatérést és mind a hat saját időjárásállapotot a headless
LVGL-teszt ellenőrzi. Az energiautaknál külön vizsgálja a vonal/pont pályaazonosságát,
a pozitív és negatív irányt, a nullát és a 7/15 kW sebességegyezést.
A görbeanimáció és a teljes ikonkatalógus a [sablonleírásban](templates.md)
megtekinthető. Az előnézetek frissítése az LVGL render után:

```sh
python3 tools/prepare_template_previews.py .preview-images
```

Fizikai kijelzőn a végső átvételhez még ellenőrizd a tényleges SD-olvasást,
a szabad memóriát, az animáció simaságát és a webes telepítést.

## Sablononkénti olvashatóság és paletta

A scene opcionális `gradient` színe vízszintes színátmenetet ad a `background` mellett. `palette`: `pv`, `home`, `grid`, `battery` hexaszínek, a diagramokkal közösen. A value/label elemek `text_shadow: true`, `shadow_offset: 0..4`, `shadow_opacity: 0..255` beállítása fekete szövegárnyékot ad. A `font` és `color` elem szintjén állítható. A rendszer alapnézetét ezek a sablonbeállítások nem érintik.
