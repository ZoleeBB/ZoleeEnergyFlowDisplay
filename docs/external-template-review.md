# Külső témák célzott v3 ellenőrzése

Ellenőrzés: 2026-10-21, a 15. checkpoint után. A futtatókód változatlan;
a cél a meglévő, kis ráfordítással használható deklaratív csomagformátum
és a v3 két opcionális elérési pontjának áttekintése volt.

| Terület | Eredmény, korlát |
| --- | --- |
| Azonosító | Nincs általános, gyári ID-kre szűkítő engedélylista. Legfeljebb 32 kisbetű/szám/kötőjel/aláhúzás. A `builtin` és a speciális `loxone-home` szerepe foglalt. |
| Elemtípus és adatforrás | A C-validátor engedélylistás. Saját C/JS, URL vagy végrehajtható plugin nem része a formátumnak. |
| Geometria | 1280×800, jelenetenként legfeljebb 96 elem; egész pixelek, képernyőn belüli téglalapok. A flow 2–8 pontos; Bezier 4 vagy 7 ponttal. |
| Betűk | Csak a támogatott 12/16/20/28/48/96 méretek. A 48/96 készlet numerikus, ezért szöveges címhez nem használható. |
| Gyári kompatibilitási ágak | Vannak név szerint a régi gyári témákat javító szín/geometria/animáció ágak; ezek nem jelentenek általános ID-engedélylistát. Külső témánál saját ID használata szükséges, ne foglaljon el gyári ID-t. |
| Normál csomag | 32 PNG, 34 ZIP-bejegyzés, 64 KiB JSON; képenként 2,5 MiB, összesen 5 MiB kódolt és 12 MiB konzervatív RGBA-dekódolt kép. A ZIP/összes kibontott bájt felső korlátja 8 MiB. |
| PNG | Legfeljebb 1280×800, 8 bites RGB/RGBA, interlace és paletta nélkül. A teljes CRC és tömörített adattartalom ellenőrzött. |
| Webes előnézet | A pár legfeljebb 640×400. PNG-számba/tárolási keretbe beleszámít, UI assetlistából és dekódolási keretből kimarad. A két kép nélküli régi csomag támogatott. |
| Telepítés | A ZIP előzetes ellenőrzése nem aktivál. Azonos ID frissítése tranzakciós, hibás csomag a korábbi telepítést/választást nem cseréli le. |
| Otthon | Explicit `home_hotspot`, C-ben ellenőrzött egész, képernyőn belüli téglalap. A nyitás feltétele továbbra is Master/Loxone, közös erőforrás és megfelelő konfiguráció. |
| Asszisztens | Külső témánál explicit `assistant_area` szükséges. A UI ellenőrzi az egész koordinátákat, a minimum 320×46, maximum 1280×100 méretet és a teljes képernyőn belüli elhelyezést. Hiányzó/hibás helyre nem rajzol. |

Az `assistant_area` ellenőrzése jelenleg a UI kapujában történik; nem önálló
kötelező telepítési mező. Ezért egy régi vagy hibás opcionális helymegadástól
nem kell teljes csomagelutasítást várni: a javaslatpanel marad rejtve.
Ezt a dokumentált viselkedést az áttekintés nem változtatja meg.

A szerző feladata az elemek, hosszú HU/EN szövegek, érintőcélok és asszisztens
közötti átfedés elkerülése. A sémahatárok nem jelentenek automatikus vizuális
minősítést vagy fizikai eszközön garantált FPS-/PSRAM-megfelelést.
A Nap pálya kiterjesztés külső használatakor a referencia és a valódi LVGL-próba
is szükséges; nincs külön teljes külső csomagminősítési rendszer.

## Bizonyíték és folytatás

- A `python3 tests/test_templates.py` friss futása sikeres: minden telepített
  téma mindkét jelenete, referenciák, PNG CRC/kibontás/határok; nem gyári
  `test-layout` és `curved` ID-k telepítése; frissítés, aktív újratöltés,
  megőrzés, törlés, hiányzó választás visszaállása, hibás/veszélyes ZIP-ek elutasítása.
- A 15. checkpoint teljes GitHub LVGL-próbája sikeres volt. Az
  `external-test` ID-n a hiányzó, érvényes és kilógó asszisztenshely külön
  ellenőrzött. Ez egy konkrét referenciajelenet próbája, nem minden lehetséges
  külső téma vizuális vagy teljesítménytesztje.
- Új firmware-kód, új kép, új csomagformátum vagy memóriakeret-emelés nem készült.
- A szerzői specifikáció: [template-authoring.md](template-authoring.md).
  A kiválasztott konkrét külső csomag fizikai ellenőrzése későbbi feladat lehet.
