# Energiakör – 06-energy-orbit v13

A felhasználó 2026-10-21-én elfogadta a külön energiaudvaros látványtervet,
a 00:00–24:00 közötti vízszintes napállás-idővonallal. A téma azonosítója,
sorrendje és mentett választása megmarad; a csomag verziója 13.

A saját új kertben középen áll a ház, külön napelem, hálózati oszlop és
akkumulátor veszi körül. Nappal világos kert, éjjel sötétkék környezet és
világító házablakok. A két tiszta háttérben nincs felirat vagy energiaút.
A teljesítmények, SOC, irányok, napi összesítők és színes görbék élő LVGL-elemek.
A ház érintése a meglévő Master/Loxone szabályokkal nyitja a fogyasztói oldalt.
Az asszisztens a dekoratív középső fejléc helyét használhatja, fontos adat nélkül.

## Energiautak

Három külön Bezier-görbe, úthossz szerinti mozgó pontokkal:

- Napelem → ház: zöld.
- Hálózat → ház: piros; ház → hálózat: citromsárga.
- Ház → akkumulátor: sötétebb kék; akkumulátor → ház: világosabb kék.

Nulla teljesítménynél nincs mozgó pont, az inaktív út visszafogottan látszik.
A hálózati nulla érték semleges színű. A töltés/leadás felirata és értéke külön,
a tároló kártyáján látható. A korábbi v12 és régebbi csomagokhoz szükséges
renderer-kompatibilitási ágak megmaradnak.

## Valódi 24 órás idővonal

A `solar_timeline` új deklaratív elemtípus. Nem igényel új kapcsolatot,
adatforrást, FreeRTOS-taskot vagy SD-képet. A már szinkronizált helyi órát és
a meglévő energiaadat napkelte/napnyugta mezőit használja.

A teljes tartomány mindig 00:00–24:00, negyednapos osztásokkal. Az események
és a jelenlegi idő helye ugyanazon a skálán `perc / 1440`; nem a napkelte és
napnyugta közé nyújtott órát mutat. A napkelte és napnyugta közötti szakasz
arany, a két éjjeli szakasz kékesszürke. Az aktuális jelölő nappal nap,
éjjel hold, a tényleges helyi idő és az események alapján; nem a kézi
világos/sötét megjelenés határozza meg a jelölő fajtáját.

- A jelölő csak szinkronizált, legalább 2020-as helyi óránál jelenik meg.
- Napkelte/napnyugta csak érvényes `sun_position_valid` és
  `0 <= napkelte < napnyugta < 1440` esetén jelölhető.
- Hiányzó/érvénytelen eseményadat: semleges alapvonal, időpontok helyén `-`,
  nincs kitalált nappali sáv vagy nap/hold ikon. Szinkronizált óránál semleges
  pont mutatja a helyi időt; ismeretlen óránál ez is rejtett.
- A két eseményfelirat rövid nappalnál elkülönül; a jelölővonalak helye
  változatlanul a pontos adatot követi. A jelenlegi idő a panel jobb felső sarkában is látszik.
- Éjfélkor a jelölő a bal oldalra kerül. Az idővonal helyi naptári óra,
  nem a nyári/téli óraátállítás napján eltelt órák energia-integrálja.

A widget a meglévő `template_node_t` mezőit használja; nincs új globális
csomag- vagy pályapuffer. Az állandó feliratokat egyszer hozza létre, az élő
feliratokat és színeket csak változáskor állítja. Az ikonok a már beágyazott
kis időjárásképek; a háttér natív méretű marad.

## Képek és telepítés

| Fájl | Méret | Bájt |
| --- | --- | ---: |
| `templates/06-energy-orbit/day.png` | 1280×800 RGB | 921894 |
| `templates/06-energy-orbit/night.png` | 1280×800 RGB | 711746 |
| `preview-light.png`, `preview-dark.png` | 480×300 | tényleges LVGL-render |

A beépített imagegen eszközzel az elfogadott tervről eltávolítottuk a teljes
UI-t, értékeket, színes utakat és pontokat. A kamera, a ház, a kerek burkolat,
a napelem, oszlop és tároló helye megmaradt. Az éjszakai szerkesztés csak a
megvilágítást változtatta: sötétkék kert, meleg házablakok, sötét napelem;
se UI, se új energiaút nem került a háttérre. A nyers nagy képek nem kerülnek
repo-/telepítőcsomagba.

Előkészítés: Lanczos 1280×800, az RGB565-ben elvesző alsó csatornabitek
nullázása (R/B `& 248`, G `& 252`), majd veszteségmentes oxipng. A méretezett
kiindulókép RGB565-színei megmaradnak. A PNG 8 bites RGB marad, nem palettás.
A meglévő fájl-/kép-/cache-keretek nem nőnek; a három korábbi alsó ikon marad,
a már nem használt külön akkukép kikerül. A SHA256-ok a manifestben szerepelnek.

Az új `solar_timeline` miatt **előbb az új Master firmware-t**, utána a
`06-energy-orbit.zip` csomagot telepítsd a webes sablonkezelőben. A korábbi
firmware még nem ismeri az új elemtípust. Az OTA önmagában nem írja át az SD-s
témát; azonos ID-vel a ZIP frissíti a meglévő csomagot. A 4B külön firmware
nem kap új Master-témaképernyőt.

## Ellenőrzés és fennmaradó határok

A valódi LVGL-próba mind az 1440 helyi percet, az éjféli visszaállást,
napkelte/napnyugta helyét és nap/hold váltását ellenőrzi. Hiányzó, fordított,
egyező és tartományon kívüli napadat, nem szinkronizált/1970-es óra, rövid
nappal, két nyelv, 15 kW és 100% is szerepel. A tényleges animációval mindkét
energiairány ellenőrzött; az asszisztens nem fedheti az idővonalat sem.
A külső csomag validátora az új típus legalább 480×132 méretét is ellenőrzi.
A képek és tesztértékek szintetikusak. A teljes firmware/OTA-méretet és CI-t
a fejlesztési terv zárása rögzíti; fizikai paneles RAM/DMA/PSRAM/FPS-próba
és a valós Miniserveres tereppróba külön hátravan.
