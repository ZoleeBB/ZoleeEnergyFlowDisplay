# USB első telepítés – Zolee Energy Flow Display

Célhardver: Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1. A ZIP az adott release összes szükséges flash-képét tartalmazza, a fordítás által meghatározott címekkel.

1. Csomagold ki a ZIP-et egy saját mappába. Telepíts Python 3.10 vagy újabb verziót.
2. Windows PowerShellben nyisd meg ezt a mappát, majd futtasd:

   ```powershell
   py -m pip install esptool==5.1.0
   py flash.py --port COM5 --dry-run
   py flash.py --port COM5
   ```

   A COM5 helyére az Eszközkezelőben látható ESP32-P4 soros port kerül. Linux/macOS alatt `py` helyett `python3`, a port például `/dev/ttyACM0` vagy `/dev/cu.usbmodem...`.

3. Használj adatátvitelre alkalmas USB-kábelt és a panel ESP32-P4 programozó csatlakozóját. Ha nem lép automatikusan letöltési módba: tartsd lenyomva a BOOT gombot, nyomd meg és engedd el a RESET gombot, majd engedd el a BOOT-ot. Írás után RESET.
4. Az SD-kártya gyökerébe másold a saját hálózati adataiddal kitöltött `config.sample.json` fájlt `config.json` néven; HTTP módban a `config.http.sample.json` használható. A mintában szereplő címeket és jelszavakat cseréld ki.

A telepítő a `flasher_args.json` összes felsorolt képét írja ki, az OTA inicializáló képet is, ha a build tartalmazza. A fájlokat és a manifestet írás előtt SHA256 alapján ellenőrzi. A `--dry-run` csak ellenőriz és megmutatja a parancsot; nem ír az eszközre. Kapcsolati gondnál `--baud 115200` is használható.

Ez első telepítő: a flashben levő alkalmazást és OTA kiválasztási állapotot felülírja. A már működő kijelző normál frissítéséhez a release külön `-ota.bin` fájlját töltsd fel a `http://ESZKOZ-IP/update` oldalon. A ZIP-et ne töltsd fel oda. Az SD konfigurációt és statisztikát az USB telepítő nem módosítja.
