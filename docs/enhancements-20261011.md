# Tíz fejlesztés – 2026.10.11.

Az eredeti rendszer alapnézetének geometriája, ikonvilága és színei változatlanok. NightMode váltja a világos/sötét változatot, az elrendezés továbbra is kizárólag kézzel lapozható.

| Korábbi javaslat | Megvalósítás | Szakasz |
|---|---|---|
| 1. Halvány, színes hátterek | Natív színátmenetek hét választható témában; a ház illusztrációja megmarad | 2 |
| 2. Egységes háttér–ikon–betű–diagram | Közös szerepszínek és sablonpaletta, saját meglévő ikonkészletek | 2 |
| 3. Teljesítményfüggő világító utak | Finom fényudvar, 7 kW-os korlát, nulla és fordított irány kezelése | 3 |
| 4. Állítható árnyék/kontraszt/számméret | JSON font/color/text_shadow/shadow_offset/shadow_opacity | 2 |
| 5. Rövid kézi áttűnés | 160 ms belépő animáció, előzetes SD-betöltés az LVGL-záron kívül | 3 |
| 6. Heti/havi statisztika | 35 nap, naptári hét/hónap, napi oszlopok, külön jelölt adathiány | 1, 4 |
| 7. Önellátás és helyben felhasználás | Időben integrált, becsült forrásenergiák, két százalékos mutató | 1, 4 |
| 8. Költség, bevétel, megtakarítás | Napi/havi összeg a konfigurált tarifákkal, nulla nevező kezelése | 1, 4 |
| 9. Közös diagnosztika | Kijelző + helyi web/API: Wi-Fi, csomag, MQTT/HTTP, SD, firmware, memória | 5 |
| 10. Webes előnézet és validálás | Feltöltés előtti helyi nappali/sötét előnézet, verzió/képmemória, külön C-validálás | 6 |

## Új téma

`08-statistics`: termelés, fogyasztás, import/export, akkumulátorteljesítmény és SOC. Nincs grafikus energiaáramlás, az élő teljesítmények csak számszerűek. A referencia mobilképek tartalma 1280×800-as, fekvő, görgetés nélküli elrendezésbe került; a mínusz a töltést/exportot jelzi a grafikonokon, a pillanatnyi teljesítményértékek előjel nélkül, külön irányfelirattal láthatók.

## Ellenőrzési határok

A host tesztek a tényleges C integrálást, helyi naptárat, régi fájlok migrációját, előjeles és SOC-grafikonokat, forrásenergia-egyezést, ZIP-telepítőt és HTTP-kezelőt futtatják. A böngésző ZIP-olvasó valós stored/deflate csomagokkal tesztelt. A tényleges LVGL renderer mind a 16 világos/sötét sablonnézetet rendereli és kilenc elrendezésen lapoz. Fizikai ESP32-P4 kijelzőn az SD és a vizuális áttűnés átvételi próbája továbbra is szükséges.
