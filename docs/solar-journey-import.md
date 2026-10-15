# Nap pálya v2 grafikai import

A teljes újragenerált, 13 PNG-t tartalmazó grafikai készlet jelenleg külön ZIP-ben elérhető. A GitHub repo szöveges API-ja a helyi bináris fájlokat nem tudja átemelni; az eredeti PNG-k nem helyettesíthetők a régi témákból kimásolt képekkel.

A helyi Git-checkoutban futtasd:
```sh
python tools/import_solar_journey.py NapPalya-komplett-sablon.zip
python tests/run_tests.py
git add templates/09-solar-journey assets/template-manifest.json
git commit -m "assets(solar): add original day-night artwork and icon layers"
git push origin feature/solar-journey-template
```

A szkript megőrzi a teljes v2 sablonleírót, frissíti a képfájlok SHA-256 jegyzékét, visszautasítja a váratlan ZIP bejegyzéseket, és **nem** commitol automatikusan. Az importálás után az ESP-IDF, Windows simulator és a valós 1280×800 kijelző ellenőrzése szükséges. Az integráció nem tekinthető befejezettnek a fájlok push-olása előtt.
