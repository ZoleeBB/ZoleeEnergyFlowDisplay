# Nap pálya V6 – grafikai csomag

A teljes **V6** képkészlet és a `template.json` már megtalálható a `feature/solar-journey-template` ágon a `templates/09-solar-journey/` könyvtárban.

- Összesen **25 PNG**: nappali és éjszakai, azonos pályageometriára igazított háttér; külön, átlátszó mozgó napkorongok; időjárás-, energiaáramlás- és összegzőkártya-ikonok.
- A háttérre rajzolt fényes napív és a horizont alatti kék ív a képfájl része; **külön `solar_arc` elem nincs**. A napkorong egyedül mozgó képréteg, a háttér által meghatározott koordinátákon.
- Az egyes PNG-k a sablon natív ZIP-feltöltőjén keresztül is telepíthetők, ha a `NapPalya-V6-web-feltoltes.zip` csomag elérhető.
- A firmware és a helyi tesztek a repository **`assets/template-manifest.json`** fájlját használják: minden sablon PNG-fájljának SHA-256 ellenőrzőösszegét tartalmazza.
- A V6 javítócsomagban szereplő **`docs/asset-manifest.json`** csak a 25 képet dokumentáló *külön ellenőrzőlista*. **Ezt nem kell feltölteni a repóba**, mert a központi manifest már tartalmazza az adatokat.

## Ellenőrzés

```sh
python3 tests/test_templates.py
python3 tools/prepare_web.py --check
python3 tests/run_tests.py
```

A firmware valódi 1280×800-as képernyőn történő vizuális ellenőrzése továbbra is szükséges; a PR egyesítése csak ezután javasolt.
