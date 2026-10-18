# OTA és helyi webfelület

## Automatikus éjféli rendszerfrissítés

A Beállítások → Frissítés → **Automatikus rendszerfrissítés éjfélkor** kapcsolóval
engedélyezhető. A `config.json` mezője `ota.auto_update` (0/1), alapértéke **0**.
A kapcsoló mentése újraindítást kér, de induláskor nem keres és nem telepít frissítést.

A működő kijelző minden előrehaladó **helyi napváltáskor** egyszer ébreszti a
frissítő háttérfeladatot. A statisztikai integrálási mintaköz végét megvárja
(alapból 31 másodperc), majd a beégetett
`https://api.github.com/repos/ZoleeBB/ZoleeEnergyFlowDisplay/releases/latest`
végponton ellenőrzi a legújabb kiadást. Nincs külön szerver, token vagy URL-beállítás.

- Csak szigorúan újabb, stabil `vMAJOR.MINOR.PATCH` verziót fogad el; draftot,
  előzetes kiadást, azonos verziót és downgrade-et nem telepít.
- A kiadásnak tartalmaznia kell a megfelelő nevű
  `ZoleeEnergyFlowDisplay-vX.Y.Z-ota.bin` és `SHA256SUMS.txt` fájlt.
  A még készülő, hiányos release-t kihagyja. Nem a USB ZIP-et tölti le.
- A GitHub API és az átirányított letöltés is tanúsítvány- és hostname-ellenőrzött
  HTTPS-t használ. HTTP-ra vagy idegen hostra nem követ átirányítást.
- A letöltés közvetlenül az inaktív OTA-partícióra kerül, korlátozott pufferrel;
  ellenőrzi a méretet, a firmware projektnevét és verzióját, a teljes fájl SHA-256
  ellenőrzőösszegét és az ESP-IDF image-validációját.
- Csak sikeres, közvetlenül aktiválás előtti SD-statisztikamentés után állítja át
  a bootpartíciót, majd újraindul. A már meglévő háromperces OTA-visszaállítási
  védelem megmarad. Korábban visszagörgetett azonos verziót automatikusan nem
  próbál újra; a következő újabb release-re vár, vagy kézi OTA használható.
- Nincs párhuzamos kézi OTA, konfigurációmentés vagy webes újraindítás: ilyenkor
  a webes módosítás 409 választ ad, illetve a foglalt éjféli ellenőrzés kimarad.
- Internet/Wi-Fi hiányakor, GitHub limitnél, hibás/csonka letöltésnél vagy SD-hibánál
  a jelenlegi firmware marad. A következő helyi napváltáskor próbálkozik újra;
  nincs nappali ismétlés és nincs bekapcsoláskori bepótlás.

A naplóban megjelenik az ellenőrzés, az új verzió letöltése, a siker vagy a hiba.
Slave módban is használható, **de saját Wi-Fi és internet kell hozzá**; a master
Wi-Fi-beállítását és auto-update kapcsolóját PIN-es párosítás után automatikusan
átveszi. A Bluetooth-kapcsolat nem továbbít firmware-t. A sablonokat, az SD-konfigurációt,
a C6 firmware-t, a bootloadert és a partíciós táblát nem cseréli.
Ez kizárólag az aktuális 10,1 colos ESP32-P4 firmware frissítési csatornája;
eltérő hardverhez külön OTA-csatorna szükséges.

A HTTPS és a kiadáshoz mellékelt ellenőrzőösszeg a hálózati és fájlhibák ellen véd;
ez nem külön firmware-aláírás. A repo/release írási jogait továbbra is védeni kell.

Hostname:

`zolee-energy-flow-display`

Információs oldal:

`http://zolee-energy-flow-display.local/`

Firmware feltöltés:

`http://zolee-energy-flow-display.local/update`

## Hitelesítés

A `ota.username` és `ota.password` mezők közös HTTP Basic hitelesítést adnak az információs oldalhoz, az OTA feltöltéshez és az eszköz újraindításához, a sablonkezelőhöz és a diagnosztikához.

Ha mindkettő üres, nincs hitelesítés.

## Két OTA partíció

A firmware két alkalmazáspartíciót használ. Az új image először próbaállapotban indul.

Az új firmware legfeljebb 3 percen belül akkor minősül sikeresnek, ha:

- SD-kártya elérhető
- konfiguráció sikeresen betöltődött
- Wi-Fi kapcsolat létrejött
- NTP szinkron sikeres
- MQTT módban a brokerkapcsolat létrejött; HTTP módban a fogadó szerver elindult

Érvényes mérési csomag érkezése nem feltétele az OTA firmware elfogadásának.

Ha a feltételek nem teljesülnek, a bootloader visszaáll az előző működő firmware-re.

## Információs oldal

Megjeleníti többek között:

- firmware verzió
- uptime
- IP cím
- Wi-Fi RSSI
- MQTT kapcsolat állapota
- utolsó érvényes mérési csomag ideje
- SD állapot
- reset oka
- legutóbbi fontos események / hibák

Az oldalon külön megerősítéssel újraindítható az eszköz.

A közös diagnosztika a `/diagnostics` és `/api/diagnostics` útvonalon érhető el. A `POST /api/energy` adatfogadás hitelesítés nélkül működik; [HTTP útmutató](http-input.md).

## SD-beállítások böngészőből

A `/settings` oldalon a `config.json` minden dokumentált paramétere módosítható. A `GET /api/settings` az aktuális beállításokat adja vissza, a jelszavakat `null` értékkel helyettesíti (nem küldi ki). A `POST /api/settings` JSON tartalmú, `X-Zolee-Settings: 1` fejlécet igényel; a `null` jelszó megtartja a korábbit, a nem üres szöveg módosítja, az üres szöveg törli.

Mentéskor a firmware ideiglenes fájlba ír, a tartalmat ugyanazzal a konfigurációs validátorral ellenőrzi, majd a régi fájlról biztonsági másolatot készít. Egy megszakadt fájlcsere esetén a korábbi `config.json.webbak` következő indításkor visszaállítható. A statisztikai mentési és integrációs paraméterek újraindítás nélkül érvényesülnek, minden más konfigurációváltozás automatikus újraindítást indít.

**Fontos:** A Basic Auth titkosítás nélkül, HTTP-n működik, ezért a beállításkezelést csak megbízható helyi hálózaton használd. Az üres adminisztrátori felhasználónév/jelszó azt jelenti, hogy a felület nincs védve.

## Biztonságos firmware-frissítés

A feltöltőoldal százalékos előrehaladást és egyértelmű siker/hiba üzeneteket mutat. Az OTA firmware csak az utolsó statisztikai SD-mentés sikeres befejezése után aktiválódik. Kézi újraindítás előtt is azonnali mentés fut; ha ez sikertelen, az újraindítás elmarad és hibaüzenet jelenik meg.

Az eseménynapló a legutóbbi 20 RAM-bejegyzést jeleníti meg. A napló az eszköz újraindításakor törlődik, nem tartós hibahistória.

A gyári Napi mérleg, Nyugodt otthon, Energiakör és Energiafőkönyv témák szín-/háttérjavításait az OTA után a kijelző a régebbi SD-sablonokon is alkalmazza. A teljesen új sablonfájlok telepítéséhez továbbra is a webes sablonkezelő vagy az SD-re másolás használható.

Loxone módban az új firmware OTA-érvényesítéséhez a működő webfelület elegendő; a kezdeti párosítás közbeni adathiány nem okoz visszaállítást. A v2.0.0 megőrzi a korábbi általános beállításokat; a Loxone-párosítás külön fájlban van. A régi szöveges Loxone nappálya-párosítás helyett négy numerikus állapotot kell választani; az MQTT/HTTP szöveges protokoll változatlan.
