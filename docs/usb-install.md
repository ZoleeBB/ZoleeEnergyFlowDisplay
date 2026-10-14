# USB első telepítés – Zolee Energy Flow Display

Célhardver: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1. A ZIP az adott release összes szükséges flash-képét tartalmazza, a fordítás által meghatározott címekkel.

1. Csomagold ki a ZIP-et egy saját mappába. Telepíts Python 3.10 vagy újabb verziót.
2. Windows PowerShellben nyisd meg ezt a mappát, majd futtasd:

   ```powershell
   py -m pip install -r requirements-usb.txt
   py flash.py --port COM5 --dry-run
   py flash.py --port COM5
   ```

   A COM5 helyére az Eszközkezelőben látható ESP32-P4 soros port kerül. Linux/macOS alatt `py` helyett `python3`, a port például `/dev/ttyACM0` vagy `/dev/cu.usbmodem...`.

3. Használj adatátvitelre alkalmas USB-kábelt és a panel ESP32-P4 programozó csatlakozóját. Ha nem lép automatikusan letöltési módba: tartsd lenyomva a BOOT gombot, nyomd meg és engedd el a RESET gombot, majd engedd el a BOOT-ot. Írás után RESET.
4. Az SD-kártya gyökerébe másold a saját hálózati adataiddal kitöltött `config.sample.json` fájlt `config.json` néven; HTTP módban a `examples/config.http.sample.json` használható. A mintában szereplő címeket és jelszavakat cseréld ki.

A telepítő a `flasher_args.json` összes felsorolt képét írja ki, az OTA inicializáló képet is, ha a build tartalmazza. A fájlokat és a manifestet írás előtt SHA256 alapján ellenőrzi. A `--dry-run` csak ellenőriz és megmutatja a parancsot; nem ír az eszközre. Kapcsolati gondnál `--baud 115200` is használható.

Ez első telepítő: a flashben levő alkalmazást és OTA kiválasztási állapotot felülírja. A már működő kijelző normál frissítéséhez a release külön `-ota.bin` fájlját töltsd fel a `http://ESZKOZ-IP/update` oldalon. A ZIP-et ne töltsd fel oda. Az SD konfigurációt és statisztikát az USB telepítő nem módosítja.

A konfiguráció mellett a ZIP `sd-card/templates` könyvtárát is másold az SD gyökerébe. Az offline dokumentáció a `docs` könyvtárban van: [konfiguráció](configuration.md), [sablonok](templates.md), [Loxone](loxone.md).

## ESP32-P4 v3.2 és USB hibakeresés

Az esptool 5.1.0 nem megfelelő az új v3.2 chiphez. A csomag esptool 5.5.0-t kér, amely a v3.2 támogatását és új USB-OTG javításokat is tartalmazza. A firmware-ből készített flash-címek, binárisok és ellenőrzőösszegek nem változnak. Alapértelmezett soros sebesség: 115200; a `--baud` továbbra is felülírja.

Ha a kapcsolat az „Uploading stub” / „Running stub” lépésnél megszakad:

1. Zárd be a COM-portot használó terminálokat; csatlakoztasd a készüléket közvetlen USB-portra, adatkábel segítségével.
2. Lépj kézi letöltési módba: BOOT nyomva, RESET röviden, majd BOOT elengedve. Ellenőrizd a megjelent COM-port számát.
3. Futtasd a telepítő mappájában:

   ```powershell
   py flash.py --port COM3 --no-stub --manual-boot
   ```

4. Sikeres ellenőrzés után nyomj RESET-et: a kézi mód nem indítja újra automatikusan az eszközt.

A stub nélküli mód közvetlenül a chip ROM betöltőjét használja. Csak a 16 MiB alatti flash-címeket tudja írni; a telepítő ezt minden képfájlnál ellenőrzi. Normál módban a build stubbeállítása megmarad. A félbeszakadt telepítés újra elindítható ugyanazzal a teljes USB-csomaggal.

Espressif források: [v3.2 feltöltési hiba és javítás](https://docs.espressif.com/projects/esp-faq/en/latest/development-environment/firmware-update.html), [esptool kiadások](https://github.com/espressif/esptool/releases).

Sikeres feltöltés után fekete kijelzőnél lásd a [kijelzőindítás hibakeresését](errors.md). A PSRAM- és PPA-javítás a firmware része: az új teljes USB-csomagot kell használni, a `flash.py` cseréje önmagában nem elég. Soros naplóhoz zárd be a telepítőt, majd `py -m serial.tools.miniterm COM4 115200 --raw` és RESET; a COM4 helyére az induló eszköz aktuális portja kerül.
