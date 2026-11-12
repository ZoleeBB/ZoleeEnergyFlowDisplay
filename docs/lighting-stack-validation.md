# Világításvezérlés – veremvédelem és fordítási javítás

## Előzmények és változatlan korlátok

A javítás az [AGENTS.md](../AGENTS.md), a [memória- és teljesítménynapló](performance.md)
és a 2026-10-05-i specifikáció-audit A01/A02 tanulságait követi.
Az auditban a UI 20 904 bájtos snapshotja 16 384 bájtos taskvermen, a
20 808 bájtos napi betöltőstruktúra 12 288 bájtos main-vermen szerepelt.
A későbbi `sd_recovery` eseménytömb, `stats` JSON-mentés és `template_raw`
PNG/SD híváslánc ismét megmutatta: az üresjárati mért maximum és a sikeres
fordítás önmagában nem bizonyít elegendő tartalékot.

A világítás javítása nem módosít taskméretet, RTC-/PSRAM-veremelhelyezést,
DMA-tartalékot vagy a meglévő `-Werror=frame-larger-than=8192` korlátot.
Nincs új task, közös statikus munkapuffer vagy új futásidejű heapfoglalás.
A `loxone` worker 12 288 bájtos verme változatlan marad. A JSON-mélység
előzetes nyolcszintes korlátozása és a 32 KiB-os szöveghatár is megmarad.

## Igazolt fordítási hiba

A `92f34b7` forrás PR-merge buildjében az ESP-IDF 5.5.4 RISC-V GCC 14.2.0
`-Werror=format-truncation` hibát adott az `lx_light_catalog_attach` belsejében,
a `snprintf(action, sizeof(action), "%s/%s", parent, key)` kifejezésre.
A korábbi szöveges összefoglalóban szereplő 1696 bájtos keret / const-hiba
nem volt e naplóval igazolható; ezeket nem szabad mért tényként továbbvinni.

A javítás megtartja a 64 bájtos célpuffert. A szülő és a kulcs hosszát
kivonásos kapacitásellenőrzés után másolja, a perjel és a lezáró nulla
helyével együtt. Túl hosszú vagy érvénytelen címnél a kör továbbra sem
vezérelhető: nincs csonkolás, és hibás explicit `uuidAction` mellett sem
választunk találgatott tartalékcímet. Ezen az úton a variadikus formázóhívás
is megszűnik; a figyelmeztetést nem kapcsoljuk ki.

## Automatizált veremellenőrzés

A forráskódbeli `tests/test_lighting.py` a meglévő ASan/UBSan-próbák
mellett külön címhatár-tesztet futtat, és `-O0`/`-O2` hostfordítást készít
sanitizer nélkül, `-fstack-usage` kapcsolóval. Így nem a sanitizer által
megnövelt keretből következtetünk a firmware-re.

A Master `main/CMakeLists.txt` beállítása az `app_loxone_model.c`,
`app_loxone.c` és `app_ui.c` tényleges célhardveri fordításából is elkészíti
az oldalfájlokat. A komponens elkészültekor a
`tools/check_lighting_stack.py` az emitált világítási
függvényekre **1536 bájtos** felső határt vizsgál, kiírja a hívók helyi
kereteit, és hiányzó / ismeretlen / nem korlátos adatoknál megállítja a buildet.
A `.su` oldalfájl és a Python-ellenőr nem kerül a futó firmware-be.
Az itt megadott forrásútvonalak a Git-repóra vonatkoznak, nem az USB-telepítő
fájljai: a telepítéshez nincs szükség a teljes fejlesztői tesztkészletre.

A GCC `static` értéke fix helyi keret. A `dynamic,bounded` jelölés is
fordításkor bizonyított maximum: például a hívási argumentumok átmeneti
veremhelyét is magában foglalja. Mindkettőre ugyanaz az 1536 bájtos korlát
érvényes; a puszta `dynamic` nem elfogadható. Ezt külön a
`tests/test_lighting_stack.py` próbái védik, a határ +1 bájtos
átlépésével és a hiányzó adat próbájával együtt.
Forrás: [GCC – Developer Options, fstack-usage](https://gcc.sourceware.org/onlinedocs/gcc/Developer-Options.html).

## Amit ez nem bizonyít

A keretméret nem a teljes taskverem. Az inline függvények a hívóba kerülhetnek;
a hívási lánc, cJSON/libc/LVGL, megszakítások és ritka hibaágak további vermet
igényelnek. A célhardveri mérésnél a hosszú neveket, maximális kör-/képszámot,
görgetést, HSV/TW húzást, lapváltást, mentést, kapcsolatvesztést, újracsatlakozást
és kaputelefonos megszakítást is be kell járni. Közben a
`/api/diagnostics/tasks` minimumát és a legnagyobb szabad DMA-blokkot kell
figyelni; a 768 bájt alatti maradékra adott figyelmeztetést nem szabad elnyomni.
A javítás nem állít fizikai panelen mért veremmaradékot vagy FPS-eredményt.
