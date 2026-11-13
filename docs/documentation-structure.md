# A dokumentáció szerkezete és karbantartása

[Dokumentáció](README.md) · [Fejlesztés](development/README.md)

## Három szint, eltérő feladat

A gyökér `README.md` a projekt rövid bemutatása: alapadatok, indítási lépések,
felsorolásszerű funkciók, téma-előnézetek és a dokumentáció elérhetősége.
Egy új funkció részletes képei, protokollja, implementációja és átvételi naplója
nem kerül vissza a README elejére.

A `docs/README.md` a teljes dokumentáció kezdőlapja. A témaköri `README.md`
oldalak a telepítést, funkciókat, integrációkat, témákat, karbantartást,
fejlesztést, történeti terveket és verziókat különítik el.

A részletes útmutatók meglévő fájlneve és helye stabil marad. A témaköri oldalak
hivatkoznak rájuk, nem készítenek párhuzamos másolatokat. Így a régi URL-ek,
fejezetcím-horgonyok, forráskódbeli utalások és offline dokumentációs linkek
sem vesznek el. Ez tartalom- és navigációs átszervezés, nem a teljes fájlfa
kockázatos, tömeges átnevezése.

## A 2026-11-13-i átrendezés tartalommegőrzése

Kiindulási main commit: `668619d8f78c20968bbd5b8d0bc2605f77ed37d9`.
A korábbi `docs/` könyvtár mind az **53 Markdown-dokumentuma** és valamennyi
képe változatlan tartalommal és útvonalon megmaradt. A fő README-ből kikerült
részletes szövegek nem összefoglalással vagy törléssel, hanem áthelyezéssel
kerültek az alábbi helyekre; a relatív hivatkozások az új helyhez igazodnak.

| Korábbi README-rész | Új hely |
| --- | --- |
| Részletes bevezetés, újdonságok, világítási képek | [Részletes projektáttekintés](project-overview.md) |
| Teljes első indítás, Slave és adatküldési részletek | [Első indítás](first-start.md) |
| Bővebb funkcióleírások és Otthon kaputelefon-kijelzőként | [Részletes projektáttekintés](project-overview.md#funkciók-és-üzemmódok) |
| Összes dokumentációs hivatkozás | [Témaköri tartalomjegyzék](README.md#témakörök) |
| Fejlesztői parancsok, kijelző- és memóriamagyarázatok | [Fejlesztői ellenőrzés](developer-checks.md) |
| Kiadási és fejlesztési naplóhivatkozások | [Verziótörténet](versions/README.md) |
| Téma-előnézeti táblázat, logó és támogatási gombok | [Fő README](../README.md#téma-előnézetek), változatlan képekkel |

A dátumhoz kötött régi tervek és ellenőrzési eredmények nem kerülnek törlésre
akkor sem, ha a funkció azóta elkészült. A [történeti gyűjtemény](history/README.md)
jelzi a szerepüket; egy régi teszteredményt nem nevezünk át friss mérésnek.

## Új dokumentum vagy átszervezés ellenőrzése

Új részletes leírásra a megfelelő témaköri kezdőoldalról hivatkozz. A teljes
`docs/` könyvtár minden Markdown-oldala legyen elérhető a fő README-ből a
helyi hivatkozásokat követve; nem szükséges mindet közvetlenül ott felsorolni.
Új képet a hozzá tartozó útmutatóba illessz, ne lejáró Actions-artifactra hivatkozz.
A téma-előnézetek kivételével a funkciók részletes képei a dokumentációban maradnak.

A repó gyökeréből futtasd:

```sh
python3 tests/test_documentation.py
python3 tests/test_workflows.py
python3 tests/run_tests.py
```

A dokumentációteszt ellenőrzi a helyi fájlhivatkozásokat és a többszintű
elérhetőséget; a workflow-teszt őrzi az öt statikus világítási kép meglétét,
felbontását és a részletes útmutatóbeli hivatkozásait. A render- és
memóriatesztek az előnézeti artifact megszüntetése után is megmaradnak.

Az USB-telepítő a dokumentációt és annak képeit is tartalmazza. Az útmutatókban
szereplő fejlesztői parancsok a forrásrepóra vonatkoznak, nem az USB-csomagra.
A dokumentációs képek nem a firmware vagy az SD-s témák futásidejű erőforrásai.
