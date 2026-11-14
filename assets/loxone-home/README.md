# Loxone Otthon – közös ikonkészlet

A v4 csomag **79 egységes, valósághű ikont** tartalmaz a fogyasztói és a Loxone-kártyákhoz.
Mindegyik különálló **96×96 px, RGBA, átlátszó PNG** az `icons/` mappában.
A nagy generált eredetik nincsenek a repóban. Az Audio-kártya két külön,
**292×200 px** méretű nappali/éjszakai lejátszóképe a `skins/` mappában van;
ezek nem 96×96-os ikonok.

## Katalógus

A `catalog.json` a szerzői igazságforrás. Egy tétel stabil `id` kulcsot,
magyar (`hu`) és angol (`en`) nevet, kategóriát és relatív `file` útvonalat ad.
A tárgyleírás, pontos prompt, generátor és SHA256 ellenőrzőösszegek az eredet
követését szolgálják. A képek az `image_gen` eszközzel készültek. A 64 korábbi ikon stabil
azonosítója és fájlja megmarad; a magyar és angol megjelenítési nevek közös
csoportelőtagot kapnak, például „Lámpa - mennyezeti”, „Lámpa - oldalfali”.
Két korábbi firmware-kép változatlanul ide költözött, és 13 új funkcióikon
egészíti ki a készletet.

A készlet tartalmazza a kért háztartási gépeket és klímaváltozatokat, továbbá
fagyasztót, kisgépeket, robotporszívót, számítógépet, NAS-t, routert, szivattyúkat,
érzékelőket, nyílászárókat, árnyékolókat, biztonsági és kerti eszközöket,
többféle lámpát és öt otthoni jelenetet.

## Ellenőrzés és bővítés

A repó gyökeréből, Pillow-val:

```sh
python3 tools/import_home_icons.py --complete
```

Az ellenőrzés hibát jelez hiányzó/felesleges fájlnál, hibás méretnél,
színmódnál, átlátszóságnál vagy SHA256-nál. A CI ugyanezt futtatja.

Új ikonhoz először a szerzői katalógust kell bővíteni, majd valódi átlátszó
RGBA képet generálni. A külső importjegyzék JSON-lista, tételei:

```json
[
  {"id": "uj-eszkoz", "path": "/abszolut/ut/eredeti.png", "prompt": "A pontos generálási leírás"}
]
```

```sh
python3 tools/import_home_icons.py /kulso/ut/import.json
python3 tools/import_home_icons.py --complete
```

Az import a kért méretre alakít, középre helyez és rögzíti az eredetet;
nem fest át és nem távolít el hátteret. Az eredeti megmarad a külső helyén.
Mindig a tényleges 96×96 változatot is ellenőrizni kell világos és sötét alapon.

## Integráció és SD-csomag

A `/loxone` webes szerkesztő az ikonkatalógust használja. A firmware-be csak
a tömör id/hu/en lista kerül; a PNG-k és a generálási leírások nem.
A képek külön, nem lapozható SD-erőforráscsomagot alkotnak:

```sh
python3 tools/prepare_home_resources.py
```

Az eredmény `dist/templates/loxone-home.zip`, amely a meglévő témakezelővel
telepíthető és frissíthető. A v4 ZIP **89 bejegyzést** tartalmaz:

| Tartalom | Darab | Méret |
|---|---:|---|
| Különálló ikon | 79 | 96×96 px |
| Otthon- és fogyasztói háttér | 4 | 1280×800 px |
| Audio-lejátszó képe | 2 | 292×200 px |
| Nappali/éjszakai előnézet | 2 | Legfeljebb 640×400 px |
| Loxone-logó | 1 | Legfeljebb 256×256 px |
| `template.json` | 1 | Schema 1, csomagverzió 4 |

Az [IRC termosztát](../../docs/irc-controller.md) `thermostat-irc` és a
[Redőnyvezérlő](../../docs/shading-controller.md) `blinds-shading` képe az
SD-csomag része. A korábban jóváhagyott képek geometriája és a mentett
azonosítók megmaradnak. Ennél a két kártyatípusnál továbbra sincs ikonválasztó.
Az általános `thermostat` és `blinds` ikon külön választható kép marad.
A redőnyvezérlő képe az általános `blinds` PNG bájtazonos másolata.

A kapu hat típusa külön képet használ: `gate-garage`, `gate-left`, `gate-right`,
`gate-double`, `gate-slide-left`, `gate-slide-right`. A választás automatikusan
a Loxone-blokk numerikus típusából következik; ismeretlen típushoz a `gate`
semleges kép tartozik. A dimmer, Slider és időzített kapcsoló funkcióikonja
lecserélhető. A központi világítás, nyílászáró-összesítő, motoros ablak és
Audio kötött képet használ.

A jobbra tolódó kapu (`gate-slide-right`) a jóváhagyott balra tolódó
(`gate-slide-left`) ikon pontos vízszintes tükörképe, az alfa-csatornával
együtt. A katalógus a forrásikont, az átalakítást és mindkét fájl
ellenőrzőösszegét rögzíti; az ikonazonosítók és a típusválasztás megmaradnak.

### Telepítés és visszaállítás

1. Először telepítsd a v4 csomagot támogató új firmware-t.
2. Ezután a Sablonok oldalon ellenőriztesd és telepítsd a hozzá tartozó
   `loxone-home.zip` v4 csomagot.
3. Az Otthon-oldalon és a webes szerkesztőben ellenőrizd az új képeket.

Az új firmware a korábbi v1–v3 csomagokat is elfogadja. A régi csomagban
nem deklarált új funkcióikon hiánya nem teszi használhatatlanná az egész
Otthon-oldalt: a kártya szöveges megjelenítést használ. IRC-nél nem kerül a
felirat egy eltérő geometriájú általános termosztátképre; redőnynél a régi
`blinds` kép használható. A webes előnézet csomagfrissítést jelez.
A csomagban deklarált, de hiányzó vagy sérült kép továbbra is hiba.

A régi firmware a v4 csomagot nem támogatja. **Visszaállításkor a firmware-t
és az Otthon-csomagot együtt kezeld:** az új firmware használata mellett
előbb állítsd vissza a korábbi v1–v3 csomagot, utána a hozzá tartozó firmware-t.
A frissítés előtti konfigurációmentést is őrizd meg, mert az új kártyatípusokat
a korábbi firmware nem tudja értelmezni. A csomagkezelő a validált új csomagot
staging könyvtárból cseréli be, a korábbi csomag hibás telepítésnél megmarad.

### Méretkeretek és betöltés

A v4 séma **1–96 egyedi ikont** és opcionálisan a teljes Audio-képpárt fogadja.
Az `audio_skins` mező pontosan `light: audio-player-day.png` és
`dark: audio-player-night.png` értéket ad; mindkét kép 292×200-as.
A v1–v3 csomagok továbbra is pontosan 64 ikont tartalmazhatnak.

A Home-csomag kódolt PNG-kerete **6 MiB**, teljes ellenőrzési dekódolt kerete
**20 MiB**, a ZIP legfeljebb **8 MiB és 128 bejegyzés**. A normál témák
**32 PNG, 5 MiB kódolt és 12 MiB dekódolt** korlátja nem változik.
Az önmagukban érvényes képfájlok sem lehetnek a manifeszttől független extrák.

A runtime csak a megnyitott oldal képeit olvassa be. Az ismétlődő ikon egyetlen
memóriapéldányon osztozik. Az Audio-képpárból csak az aktuális nappali vagy
éjszakai kép kerül memóriába, és csak Audio-kártyát tartalmazó oldalon.
Kártyalapozáskor a változatlan háttér és logó megmarad; a kért ikonok és
Audio-kép cserélődnek. A teljes katalógus nem lesz tartós képcache.

A `backgrounds/light.png` a jóváhagyott nappali szoba kártyák/feliratok nélküli
rekonstrukciója, a `dark.png` ugyanennek az éjszakai változata. A beépített image_gen
eszközzel készültek; a nagy eredetik nincsenek a repóban. Végső méretük 1280×800 RGB.
A PNG-k a panel RGB565 színpontosságára vannak optimalizálva: a vörös/kék csatorna
alsó 3, a zöld alsó 2 bitje nulla. Ez nem változtatja meg a kijelző RGB565
színértékeit, de a két háttér együtt 3 612 352 helyett 1 946 416 bájt.
A fájlok továbbra is valódi RGB PNG-k; palettás PNG-t a csomagellenőrző nem fogad.
A nappali prompt a referenciából minden UI-elem eltávolítását és a nyugodt,
nagy ablakos, zöld környezetű nappali/konyha megtartását kérte. Az éjszakai prompt
kizárólag időjárás-/fényváltást kért, változatlan kamerával és berendezéssel,
sötétkék kerttel és a meglévő meleg beltéri világítással. Az éjszakai oldal végső
vizuális elfogadása a fejlesztési tervben követendő.

A fogyasztó 0 mérésénél valódi, luminanciaalapú szürke ARGB-változat készül;
a színes ikon nem kap egyszerű áttetsző fedőréteget. Az alfa megmarad.

A teljes folyamat ellenőrzése:

```sh
python3 tests/test_home_resources.py
python3 tests/test_loxone_home.py
```


A v2 csomag külön fogyasztói háttérpárt is tartalmaz: `backgrounds/consumers-light.png`
és `backgrounds/consumers-dark.png`. Beépített image_gen eszközzel készült külső
ház-/kertnézet, majd azonos geometriájú éjszakai fényváltozat. A pontos promptok,
ellenőrzőösszegek és végleges méretek: `backgrounds/prompts.json`. Mindkét fájl
1280×800 RGB PNG, RGB565 színpontosságra optimalizálva; a nagy eredetik nincsenek a repóban.
Az Otthon-lapok továbbra is a korábbi beltéri háttérpárt használják.
A háttérpár a v4 csomagban is megmarad; a fenti aktuális fájlszámok és
memóriakeretek az új ikonokat és az Audio-képeket is tartalmazzák.
