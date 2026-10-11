# Fejlesztési javaslatok — 2026-10-11

A projekt célja egy mindig bekapcsolt, helyi otthoni energia-kijelző: gyorsan
érthető pillanatnyi energiairányok, akkumulátorállapot, megbízható napi
mérlegek és hosszabb távú összehasonlítás. A további funkcióknak ezt érdemes
szolgálniuk, a stabil megjelenítést és adatfogadást megőrizve.

## A jelenlegi megvalósításból következő korlátok

- A felület 1280×800, RGB565; három panelpuffer van konfigurálva és a
  kijelzőadapter `TRIPLE_PARTIAL` módot használ. Egy teljes RGB565 képkocka
  2 048 000 bájt, három ilyen puffer ~5,86 MiB, a további adapter- és
  rajzpufferek nélkül. Ez konfigurációból számított becslés, nem heapmérés.
- A dekódolt képcache 6 MiB. A sablonkorlát 96 elem/jelenet, 32 PNG,
  összesen 4 MiB tömörített és 12 MiB névleges dekódolt kép. Utóbbi a két
  jelenet összes képére vonatkozó ellenőrzési keret, nem egyszerre lefoglalt
  cache. A háttér RGBA-dekódolása akár ~3,91 MiB; a sablonváltás átmeneti
  csúcsát külön kell mérni.
- A UI állapotfrissítése 200 ms, az animáció 16 ms, a grafikonoké 5 s.
  A periódusstatisztika lekérdezése szintén 5 s. Az adatforrás mintája 5 s.
  Így a grafikák és az áttetsző rétegek ismételt keverése valószínűbb
  szűk keresztmetszet, mint a JSON feldolgozása. Ezt hardvermérésnek kell
  megerősítenie; a szimulátor nem igazolja a készülék FPS-ét.
- Már létezik timeout, adathézag-jelzés, napi biztonságos `.tmp`/`.bak`
  mentés és fsync, OTA rollback, közös diagnosztika, sablon-erőforráskorlát
  és automatizált firmware/UI regresszió. Ezeket nem kell újra megvalósítani.
- A Waveshare aktuális dokumentációja az eszközcsaládot ESP32-P4/C6,
  kétmagos P4 és 32 MB PSRAM alapú rendszerként írja le. A CPU-órajel és
  szabad belső RAM tényleges értékét a futó firmware-ből kell kiolvasni;
  az adatlap önmagában nem biztosít szabad renderkapacitást.

## Javasolt sorrend

| Prioritás | Fejlesztés | Haszon | Erőforrástakarékos megoldás |
| --- | --- | --- | --- |
| 1 | Teljesítmény-diagnosztika és terhelési próba | Kiderül, melyik téma, átmenet vagy művelet lassú | FPS/renderidő átlag és p95; külön belső RAM/PSRAM, legnagyobb szabad blokk, minimális heap, task stack tartalék; összesítés másodpercenként, webes kiolvasás kérésre |
| 2 | Beállítható animációs sebesség, változásra frissítés | Egyenletesebb kijelzés, kisebb tartós CPU/memóriasávszélesség-terhelés | 30 FPS alapcél kipróbálása, opcionális 60 FPS; `dt` alapú mozgás megtartása; szöveg, ikon és stílus frissítése csak változáskor; nulla áramlásnál álló pontok; hibaoldalon animáció szüneteltetése |
| 3 | Adatminőség és frissesség jelzése | A felhasználó nem téveszti össze a hiányt, nullát és régi adatot | Kicsi adatállapot-jelzés és napi lefedettségszázalék; utolsó érvényes idő; hibás csomagszámláló; opcionális forrásidő/seq extra JSON-mezőként, az I1–I12 változtatása nélkül |
| 4 | Napi/heti/havi adatok CSV-letöltése | Külső elemzéshez, archiváláshoz hasznos | HTTP stream SD-fájlból kis fix pufferrel; nincs teljes hónapnyi adathalmaz RAM-ba töltése; letöltés ne tartsa az LVGL-zárat |
| 5 | Statisztika érintéses részletei | Egy kiválasztott időszak kW/kWh és adatminősége leolvasható | Egyetlen újrahasznált buborék a meglévő 5 perces pontokon; nincs másodpercenként tárolt új idősor |
| 6 | SOC- és akkumulátorhőmérséklet figyelmeztetési küszöbök | Korábban észrevehető rendellenesség | Konfigurálható helyi jelzés, hiszterézis és várakozási idő; nincs folyamatos hang/új hálózati szolgáltatás; becsült hátralévő idő csak megadott használható kapacitással és stabil terhelés mellett |
| 7 | Konfiguráció mentése/visszaállítása a helyi weben | SD-kártyacsere és telepítés egyszerűbb | Validált config-fájl import/export; jelszavak elrejtése az alapértelmezett exportban; atomikus fájlcsere a meglévő tárolási mintával |
| 8 | Sablonok terhelési mutatója és előkészített hátterek | Még telepítés előtt látszik egy nehéz téma várható költsége | Aktív jelenet dekódolt mérete, teljes képernyős áttetsző rétegek száma; nagy statikus maszkok előre összekeverése a háttérrel az assetkészítéskor; változó elemek önállóak maradnak |

Az első két lépést együtt javaslom: mérhető alapállapot, majd annak alapján
optimalizálás. Következő funkcióként az adatminőség-jelzést és a CSV-exportot
választanám. Kis állapot- és pufferigény mellett érezhetően növelik a kijelző
használhatóságát. A meglévő gazdag témakészlet további bővítése ráér ezután.

## Elfogadási mérés a célhardveren

Mind a kilenc témát legalább 10 percig futtatni nappal és éjjel, külön
sablonváltással, MQTT/HTTP adatfrissítéssel, SD-mentéssel és sablonfeltöltéssel.
Rögzíteni renderidőt, task késleltetést, packet feldolgozási időt, heap és
stack minimumot. 30 FPS esetén a rajzolási idő célkerete ~33 ms, 60 FPS
esetén ~16,7 ms; ezek célok, nem a jelenlegi firmware mért eredményei.
Hosszabb, 24–48 órás próba ellenőrizze a memóriafogyást és az adathézagokat.
Ne emeljük meg vakon a képcache-t vagy a pufferdarabszámot: előbb a
legnagyobb összefüggő szabad blokkokat és a váltások csúcsterhelését mérjük.

Folyamatos videóháttér, új böngészőmotor, nagy felbontású másodperces
előzménytárolás és helyben futó összetett előrejelző modell jelenleg nem
indokolt. Ha később előrejelzés kell, azt Loxone/LoxBerry vagy más külső
rendszer számolja, a kijelző egy kis, ritkán frissülő eredménycsomagot kapjon.

## Ellenőrzött források

- Projekt: `sdkconfig.defaults`, `main/app_ui.c`, `main/app_template_ui.inc`,
  `main/app_templates.c`, `main/app_stats.c`, `main/app_diagnostics.c`.
- [Waveshare célhardver-család](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-X).
- [Waveshare ESP32-P4-WIFI6](https://docs.waveshare.com/ESP32-P4-WIFI6).
