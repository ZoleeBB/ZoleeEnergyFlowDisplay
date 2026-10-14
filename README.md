# Zolee Energy Flow Display

Otthoni energiaáramlás-kijelző a Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 (SKU 33150) panelhez. A 1280×800 fekvő LVGL felület a napelem, a ház, a hálózat és az akkumulátor pillanatnyi teljesítményét, energiaútjait és napi statisztikáit mutatja.

A firmware ESP-IDF 5.5.4 és LVGL 9.5.0 alapú. MQTT vagy közvetlen HTTP POST fogadja a teljes mérési JSON-t, javasoltan öt másodpercenként. FAT32 microSD tárolja a konfigurációt, nyolc választható sablont és 35 nap statisztikáját. A világos/sötét módot a `NightMode` adat vezérli; az OTA két alkalmazáspartícióval és visszaállítással működik.

## Első indítás

1. Telepítsd a firmware-t az [USB útmutató](docs/usb-install.md) szerint.
2. A [MQTT konfigurációt](config.sample.json) vagy [HTTP konfigurációt](examples/config.http.sample.json) töltsd ki, és másold az SD gyökerébe `config.json` néven.
3. A repó `templates` vagy az USB-csomag `sd-card/templates` könyvtárát másold az SD gyökerébe.
4. Állítsd be a [Loxone adatküldést](docs/loxone.md), majd indítsd a kijelzőt. Wi-Fi és NTP szükséges az induláshoz.

Az I1–I12 bemenetek sorrendje fix. A négy időjáráskulcs kötelező, de értékük lehet `null`; ilyenkor a kapcsolódó adatok egyik témában sem jelennek meg. A Loxone minták I13 jelenlétmaszkját a bekötésnek megfelelően kell beállítani, mert a Pico C API nem tudja automatikusan felismerni a bekötetlen vezetéket. [Teljes mérés](examples/mqtt-payload.sample.json), [időjárás nélküli mérés](examples/mqtt-payload.no-weather.sample.json).

Balra/jobbra húzás lapoz a rendszernézet és a nyolc sablon között. Felfelé húzás nyitja a diagnosztikát, lefelé húzás bezárja. A helyi weboldal: `http://zolee-energy-flow-display.local/` vagy az eszköz IP-címe; `/update` firmware-frissítés, `/templates` sablonkezelés, `/diagnostics` diagnosztika. HTTP módban az adatfogadó: `POST /api/energy`.

![A nyolc sablon világos és sötét nézete](docs/previews/templates.png)

További képi ellenőrzések: [beépített ikonok](docs/previews/icons.png), [rendszernézet lapjelzővel](docs/previews/builtin-pagination.png), [közös diagnosztika](docs/previews/diagnostics.png).

## Dokumentáció

- [Hardver](docs/hardware.md)
- [Konfiguráció](docs/configuration.md)
- [JSON és MQTT](docs/mqtt.md)
- [HTTP adatfogadás](docs/http-input.md)
- [Loxone mintaprogramok](docs/loxone.md)
- [SD-kártya](docs/sd-card.md)
- [Nyolc választható téma](docs/templates.md)
- [Saját sablon készítése](docs/template-authoring.md)
- [Statisztika és számítások](docs/statistics.md)
- [Webfelület és OTA](docs/ota.md)
- [Hibakezelés](docs/errors.md)
- [Fordítás és flash](docs/build-flash.md)
- [USB telepítés](docs/usb-install.md)
- [Windows szimulátor](docs/simulator.md)
- [Automatikus kiadás](docs/releases.md)
- [Felépítés](docs/architecture.md)
- [Képek és licencek](docs/assets.md)

## Fejlesztői ellenőrzés

```sh
python3 tests/run_tests.py
python3 tools/prepare_templates.py
```

ESP-IDF környezetben: `idf.py set-target esp32p4`, majd `idf.py build`. A CI host regressziós teszteket, valódi LVGL renderelést, firmware-fordítást/csomagolást és külön Windows-szimulátortesztet futtat. A fizikai panelen való működést és a Loxone bekötést hardveres próbával kell ellenőrizni.

A személyes `config.json`, buildkönyvtárak és generált telepítőcsomagok nem részei a verziókezelt forrásnak. A beágyazott képek, betűkészletek, licencek és a generált webheader szükséges fordítási bemenetek.

Kijelzőindítási hibánál lásd a [fekete képernyő hibakeresését](docs/errors.md). A MIPI rajzpuffer PSRAM-ban van; a 270°-os forgatás a tiltott PPA helyett CPU ágon fut. Az LVGL sorosan rajzol az adapter zárolásával, külön OSAL rajzolószálak nélkül; a pufferjavítás is CPU-val történik a cache-vonalak ütközésének elkerülésére. A változatlan feliratok és stílusértékek nem váltanak ki ismételt újrarajzolást, az érintési tengelyek a kamera alul elhelyezkedő, 270°-os fekvő képhez igazodnak. A képcache 10 MiB; az általános foglalások PSRAM-ot részesítenek előnyben, 64 KiB belső/DMA tartalékkal. Az LVGL worker minden ciklusban valódi tickre blokkol a mutexen kívül. Az SDIO DMA után készülő RX csomagmásolatok PSRAM-ba kerülnek; foglalási hiba csomageldobással kezelhető újraindulás helyett.

Az NTP `sys_timeout` újraindulását a 10 napos SDK időzítő és fordítási korlátellenőrzés javítja; a kézi szinkron továbbra is a konfigurált periódust használja.
