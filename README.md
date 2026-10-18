# Zolee Energy Flow Display

Otthoni energiaáramlás-kijelző a Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 (SKU 33150) panelhez. A 1280×800 fekvő LVGL felület a napelem, a ház, a hálózat és az akkumulátor pillanatnyi teljesítményét, energiaútjait és napi statisztikáit mutatja.

A firmware ESP-IDF 5.5.4 és LVGL 9.5.0 alapú. MQTT vagy HTTP POST fogadja a teljes mérési JSON-t öt másodpercenként; a közvetlen Loxone mód WebSocket-állapotokból állítja össze ugyanazt a mintát. FAT32 microSD tárolja a konfigurációt, kilenc, szabadon sorbarendezhető sablont és 35 nap statisztikáját. A világos/sötét módot a `NightMode` adat vezérli; az OTA két alkalmazáspartícióval és visszaállítással működik.

A közvetlen Loxone mód beállítása és jelenlegi ellenőrzési korlátai: [Loxone adatkapcsolat](docs/loxone-direct.md).

## Első indítás

1. Telepítsd a firmware-t az [USB útmutató](docs/usb-install.md) szerint.
2. A [MQTT konfigurációt](config.sample.json) vagy [HTTP konfigurációt](examples/config.http.sample.json), közvetlen kapcsolathoz a [Loxone konfigurációt](examples/config.loxone.sample.json) töltsd ki, és másold az SD gyökerébe `config.json` néven.
3. A repó `templates` vagy az USB-csomag `sd-card/templates` könyvtárát másold az SD gyökerébe.
4. MQTT/HTTP esetén állítsd be a [Loxone adatküldést](docs/loxone.md). Közvetlen Loxone módban indulás után a `/loxone` weboldalon add meg a Miniservert és párosítsd az állapotokat. Wi-Fi és NTP szükséges az induláshoz.

Az I1–I12 bemenetek sorrendje fix; I13 az időjárási jelenlétmaszk, T1 pedig az opcionális, `#`-elválasztott nappályaadat. Mindkét Pico C változat (MQTT és HTTP) ezt a kiosztást használja. A négy időjáráskulcs kötelező, de értékük lehet `null`; ilyenkor a kapcsolódó adatok egyik témában sem jelennek meg. A Loxone minták I13 jelenlétmaszkját a bekötésnek megfelelően kell beállítani, mert a Pico C API nem tudja automatikusan felismerni a bekötetlen vezetéket. [Teljes mérés](examples/mqtt-payload.sample.json), [időjárás nélküli mérés](examples/mqtt-payload.no-weather.sample.json).

A Nap pálya érvényes `SunPosition` esetén lapozható. A témák weben átrendezhetők, de a Rendszer nézet mindig az első helyen marad. Balra/jobbra húzás lapoz a rendszernézet és a kilenc sablon között. Felfelé húzás nyitja a diagnosztikát, lefelé húzás bezárja. A helyi weboldal: `http://zolee-energy-flow-display.local/` vagy az eszköz IP-címe; `/update` firmware-frissítés, `/templates` sablonkezelés, `/settings` az SD-kártya beállításainak szerkesztése, `/diagnostics` diagnosztika. HTTP módban az adatfogadó: `POST /api/energy`. A diagnosztika mindhárom adatforrást jelzi, magonkénti CPU-terhelést, belső RAM-/PSRAM-használatot és SD-telítettségi sávot mutat.

| Téma | Nappal | Éjjel |
| --- | --- | --- |
| Rendszer (beépített) | ![Nappal](assets/previews/builtin-light.png) | ![Éjjel](assets/previews/builtin-dark.png) |
| Élő otthon | ![Nappal](templates/01-live-home/preview-light.png) | ![Éjjel](templates/01-live-home/preview-dark.png) |
| Energiautak | ![Nappal](templates/02-energy-paths/preview-light.png) | ![Éjjel](templates/02-energy-paths/preview-dark.png) |
| Műszerfal | ![Nappal](templates/03-instruments/preview-light.png) | ![Éjjel](templates/03-instruments/preview-dark.png) |
| Napi mérleg | ![Nappal](templates/04-daily-balance/preview-light.png) | ![Éjjel](templates/04-daily-balance/preview-dark.png) |
| Nyugodt otthon | ![Nappal](templates/05-calm-home/preview-light.png) | ![Éjjel](templates/05-calm-home/preview-dark.png) |
| Energiakör | ![Nappal](templates/06-energy-orbit/preview-light.png) | ![Éjjel](templates/06-energy-orbit/preview-dark.png) |
| Energiafőkönyv | ![Nappal](templates/07-energy-ledger/preview-light.png) | ![Éjjel](templates/07-energy-ledger/preview-dark.png) |
| Energiastatisztika | ![Nappal](templates/08-statistics/preview-light.png) | ![Éjjel](templates/08-statistics/preview-dark.png) |
| Nap pálya | ![Nappal](templates/09-solar-journey/preview-light.png) | ![Éjjel](templates/09-solar-journey/preview-dark.png) |

A webes sablonkezelő a csomagok `preview-light.png` / `preview-dark.png` képeit mutatja; régi csomagnál üres keret marad, amíg a témát ZIP-ből frissíted.

További képi ellenőrzések: [beépített ikonok](docs/previews/icons.png), [rendszernézet lapjelzővel](docs/previews/builtin-pagination.png), [közös diagnosztika](docs/previews/diagnostics.png).

## Dokumentáció

- [Hardver](docs/hardware.md)
- [Konfiguráció](docs/configuration.md)
- [JSON és MQTT](docs/mqtt.md)
- [HTTP adatfogadás](docs/http-input.md)
- [Közvetlen Loxone kapcsolat](docs/loxone-direct.md)
- [Loxone mintaprogramok](docs/loxone.md)
- [SD-kártya](docs/sd-card.md)
- [Kilenc választható téma](docs/templates.md)
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

A firmware-források egységes, jól olvasható tagolására és magyar nyelvű kommentelésére vonatkozó kötelező szabályokat a fejlesztői instrukciók (`AGENTS.md`) tartalmazzák. A kódformázás nem érintheti a beágyazott erőforrásokat és nem növelheti problémásan a firmware méretét.

```sh
python3 tests/run_tests.py
python3 tools/prepare_templates.py
```

ESP-IDF környezetben: `idf.py set-target esp32p4`, majd `idf.py build`. A CI host regressziós teszteket, valódi LVGL renderelést, firmware-fordítást/csomagolást és külön Windows-szimulátortesztet futtat. A fizikai panelen való működést és a Loxone bekötést hardveres próbával kell ellenőrizni.

A személyes `config.json`, buildkönyvtárak és generált telepítőcsomagok nem részei a verziókezelt forrásnak. A beágyazott képek, betűkészletek, licencek és a generált webheader szükséges fordítási bemenetek.

Kijelzőindítási hibánál lásd a [fekete képernyő hibakeresését](docs/errors.md). A MIPI rajzpuffer PSRAM-ban van; a 270°-os forgatás a tiltott PPA helyett CPU ágon fut. Az LVGL sorosan rajzol az adapter zárolásával, külön OSAL rajzolószálak nélkül; a pufferjavítás is CPU-val történik a cache-vonalak ütközésének elkerülésére. A változatlan feliratok és stílusértékek nem váltanak ki ismételt újrarajzolást, az érintési tengelyek a kamera alul elhelyezkedő, 270°-os fekvő képhez igazodnak. A képcache 10 MiB; az általános foglalások PSRAM-ot részesítenek előnyben, 32 KiB belső/DMA tartalékkal. Az LVGL worker minden ciklusban valódi tickre blokkol a mutexen kívül. Az SDIO DMA után készülő RX csomagmásolatok PSRAM-ba kerülnek; foglalási hiba csomageldobással kezelhető újraindulás helyett.

Az NTP `sys_timeout` újraindulását a 10 napos SDK időzítő és fordítási korlátellenőrzés javítja; a kézi szinkron továbbra is a konfigurált periódust használja.

A [memória- és képfrissítési optimalizálás](docs/performance.md) ismerteti a Waveshare/Espressif példák alapján alkalmazott fix SDIO DMA-ablakot, PSRAM geometriát, natív képcache-t és a hardveres mérés korlátait. A webes diagnosztika külön mutatja a belső RAM és a legnagyobb DMA-blokk állapotát.
