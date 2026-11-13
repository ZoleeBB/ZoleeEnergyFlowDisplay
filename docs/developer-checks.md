# Fejlesztői ellenőrzés – parancsok és műszaki háttér

[Dokumentáció](README.md) · [Fejlesztés](development/README.md)

A parancsokat a Git-repó gyökeréből kell futtatni. Az USB-telepítő a
dokumentációt tartalmazza, de nem helyettesíti a teljes fejlesztői forrásfát.

A fő README korábbi fejlesztői fejezetét változatlan tartalommal őrizzük itt.

A firmware-források egységes, jól olvasható tagolására és magyar nyelvű kommentelésére vonatkozó kötelező szabályokat a [fejlesztői instrukciók](../AGENTS.md) tartalmazzák. A kódformázás nem érintheti a beágyazott erőforrásokat és nem növelheti problémásan a firmware méretét.

A hosttesztekhez C-fordító, Node.js, valamint a cJSON, zlib és mbedTLS
fejlesztői csomagjai szükségesek. A Python-függőséget a használt környezetbe
(ajánlottan virtuális környezetbe) telepítsd:

```sh
python3 -m pip install -r tests/requirements.txt
python3 tests/run_tests.py
python3 tools/prepare_templates.py
```

ESP-IDF környezetben: `idf.py set-target esp32p4`, majd `idf.py build`. A CI host regressziós teszteket, valódi LVGL renderelést, firmware-fordítást/csomagolást és külön Windows-szimulátortesztet futtat. A fizikai panelen való működést és a Loxone bekötést hardveres próbával kell ellenőrizni.

A kaputelefon utáni visszatöltés célzott tesztje: `python3 tests/test_theme_restore.py` (időkorlát-állapotgép és forrásinvariánsok). A teszt be van kötve a teljes `tests/run_tests.py` futtatásába, de **önmagában nem bizonyítja** a valós SD-/LVGL-/PSRAM-terhelés melletti helyreállítást. A [készülékes ellenőrzési sorrend](intercom.md#ellenőrzés-és-kiadás) és a [beragadt betöltőképernyő hibakeresése](errors.md#kaputelefon-bezárása-után-beragadt-témabetöltés) külön dokumentált.

A személyes `config.json`, buildkönyvtárak és generált telepítőcsomagok nem részei a verziókezelt forrásnak. A beágyazott képek, betűkészletek, licencek és a generált webheader szükséges fordítási bemenetek.

Kijelzőindítási hibánál lásd a [fekete képernyő hibakeresését](errors.md). A MIPI rajzpuffer PSRAM-ban van; a 270°-os forgatás a tiltott PPA helyett CPU ágon fut. Az LVGL sorosan rajzol az adapter zárolásával, külön OSAL rajzolószálak nélkül; a Master `TRIPLE_PARTIAL` módban csak a változó képrészleteket rajzolja/forgatja. A CPU minden új frame elején a legutóbb leadott kész képet másolja a hátsó pufferre, majd az összes részlet után ad át új kész képet. A GUI/touch CPU1-en, a Loxone worker CPU0-n fut. A változatlan feliratok és stílusértékek nem váltanak ki ismételt újrarajzolást, az érintési tengelyek a kamera alul elhelyezkedő, 270°-os fekvő képhez igazodnak. A képcache 10 MiB; az általános foglalások PSRAM-ot részesítenek előnyben, 32 KiB belső/DMA tartalékkal. Az LVGL worker minden ciklusban valódi tickre blokkol a mutexen kívül. Az SDIO DMA után készülő RX csomagmásolatok PSRAM-ba kerülnek; foglalási hiba csomageldobással kezelhető újraindulás helyett.

Az NTP `sys_timeout` újraindulását a 10 napos SDK időzítő és fordítási korlátellenőrzés javítja; a kézi szinkron továbbra is a konfigurált periódust használja.

A [memória- és képfrissítési optimalizálás](performance.md) ismerteti a Waveshare/Espressif példák alapján alkalmazott fix SDIO DMA-ablakot, PSRAM geometriát, natív képcache-t és a hardveres mérés korlátait, valamint a gyors témaváltást (igazított SD-olvasás, egyszeri PNG-ellenőrzés), az Otthon-kártyák háttér-újraolvasás nélküli lapozását és a csak változáskor rajzoló Master/4B Slave frissítést. A fejlesztői teljesítményszabályok az [AGENTS.md](../AGENTS.md) fájlban vannak. A webes diagnosztika külön mutatja a belső RAM és a legnagyobb DMA-blokk állapotát.
