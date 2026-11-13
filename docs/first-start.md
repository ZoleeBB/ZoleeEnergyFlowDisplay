# Első indítás – részletes útmutató

[Dokumentáció](README.md) · [Telepítés és használat](getting-started/README.md)

A fő README korábbi teljes indítási leírása, a Master/Slave és adatküldési
részletekkel együtt.

1. Telepítsd a firmware-t az [USB útmutató](usb-install.md) szerint.
2. A [MQTT konfigurációt](../config.sample.json) vagy [HTTP konfigurációt](../examples/config.http.sample.json), közvetlen kapcsolathoz a [Loxone konfigurációt](../examples/config.loxone.sample.json) töltsd ki, és másold az SD gyökerébe `config.json` néven.
3. A repó `templates` vagy az USB-csomag `sd-card/templates` könyvtárát másold az SD gyökerébe.
4. MQTT/HTTP esetén állítsd be a [Loxone adatküldést](loxone.md). Közvetlen Loxone módban indulás után a `/loxone` weboldalon add meg a Miniservert és párosítsd az állapotokat. Wi-Fi és NTP szükséges az induláshoz.

Slave-ként SD-vel a minimális `config.json`: `{"DataReceptMode":"slave"}`.
Új, korábban fő kijelzőként nem azonosított eszköz SD nélkül automatikusan Slave
módban indul. A masteren kapcsold be az adat seedet, nyisd meg a párosítási ablakot,
és a diagnosztikáján látható PIN-t írd be a Slave-en. Saját MQTT/Loxone/NTP
beállítás nem kell. [Párosítás és átállítás](ble-data-seed.md).

Az I1–I12 bemenetek sorrendje fix; I13 az időjárási jelenlétmaszk, T1 pedig az opcionális, `#`-elválasztott nappályaadat. Mindkét Pico C változat (MQTT és HTTP) ezt a kiosztást használja. A négy időjáráskulcs kötelező, de értékük lehet `null`; ilyenkor a kapcsolódó adatok egyik témában sem jelennek meg. A Loxone minták I13 jelenlétmaszkját a bekötésnek megfelelően kell beállítani, mert a Pico C API nem tudja automatikusan felismerni a bekötetlen vezetéket. [Teljes mérés](../examples/mqtt-payload.sample.json), [időjárás nélküli mérés](../examples/mqtt-payload.no-weather.sample.json).

A Nap pálya érvényes `SunPosition` esetén lapozható. SD-vel a témák weben átrendezhetők, de a Rendszer nézet mindig az első helyen marad. Balra/jobbra húzás lapoz a rendszernézet és az elérhető sablonok között; SD nélkül csak a Rendszer nézet érhető el. Felfelé húzás nyitja a diagnosztikát, jobb felső X zárja. Master/Loxone módban lefelé húzás az Otthon-lapokat, a ház érintése a fogyasztókat nyitja meg, ha a szükséges csomag és konfiguráció elérhető. A helyi weboldal: `http://zolee-energy-flow-display.local/` vagy az eszköz IP-címe; `/update` firmware-frissítés, `/templates` sablonkezelés és GitHub-témafrissítés, `/settings` beállítások (SD-n vagy belső NVS-ben), `/backup` mentés és visszaállítás, `/diagnostics` diagnosztika. A gesztusok teljes listája: [Kezelés és gesztusok](gestures.md). A Slave webfelületéhez a mastertől örökölt Wi-Fi-n kapott saját IP-cím használható. HTTP módban az adatfogadó: `POST /api/energy`. A diagnosztika mind a négy adatfogadási módot jelzi, magonkénti CPU-terhelést, belső RAM-/PSRAM-használatot, SD- vagy belsőtár-állapotot és Bluetooth-adatokat mutat.
