# Master/Slave – átadási és lezárási jegyzet

Ez a [14-es PR](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/pull/14)
10,1 colos Waveshare ESP32-P4 kijelzőhöz készült fejlesztését foglalja össze.
A kisebb asztali kijelző portolása a hardver kiválasztása és beszerzése után,
új fejlesztési ágon következik.

## Elkészült működés

- Kapcsolható BLE master, automatikus Slave-keresés és újracsatlakozás,
  energia/időjárás/UTC-idő és helyi UTC-eltolás továbbítása.
- Hatjegyű master-PIN a helyi diagnosztikán, kétperces párosítási ablak és
  érintős Slave PIN-pad; SC/MITM/bonding, NVS-ben tárolt párosítás.
- Titkosított masterprofil: Wi-Fi SSID/jelszó és automatikus OTA-kapcsoló
  öröklése, a Slave saját DHCP-címével.
- Kétoszlopos diagnosztika, Bluetooth szerep/cím/partner/RSSI és master
  kapcsolatszám, továbbá SD nélküli belső tár és statisztikai frissesség.
- Kapcsolható, helyi napváltáshoz kötött stabil GitHub-release OTA, SHA-256-
  és image-ellenőrzéssel, közös műveletkizárással és rollback-validációval.

| Slave változat | Téma | Statisztika | Beállítások |
| --- | --- | --- | --- |
| SD-vel | Rendszer + kilenc telepített téma; Nap pálya érvényes nappályaadattal | Saját integrálás és legfeljebb 35 nap saját SD-előzmény | SD `config.json`, a master hálózati profilja külön NVS-ben |
| SD nélkül | Csak Rendszer | Master napi pillanatképe RAM-ban; 30 s-os szinkron, teljeskép-ellenőrzés, elavultságjelzés | Belső NVS JSON, legfeljebb 3072 bájt, rögzített Slave szerep |

Az SD-s Slave a saját adatgyűjtésétől épít történetet. Az SD nélküli Slave
újraindítás után a mastertől tölti vissza a diagramokat. Ismert fő kijelző
SD-hibája nem kapcsolja át az eszközt Slave-re. Átállítás és párosítás:
[BLE útmutató](ble-data-seed.md), [SD-változatok](sd-card.md#slave-sd-kártyával-és-anélkül).

## Igazolt automatizált ellenőrzések

A firmware-kód ellenőrzött commitja:
[`490b6afa66f084a7b758fff36ddaba175c13d0f5`](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/commit/490b6afa66f084a7b758fff36ddaba175c13d0f5).
A lezáráshoz készített dokumentáció ezt a kódot nem módosítja.

| Ellenőrzés | Eredmény és bizonyíték |
| --- | --- |
| Natív regressziók, BLE/PIN/NVS/OTA és JavaScript | Sikeres helyi és [GitHub regresszió](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39129299122) |
| Valódi LVGL 9.5 UI, PIN-pad, négy SD nélküli állapot és mind a 18 SD-s témarender | Sikeres helyi és GitHub UI-preview, ugyanabban a futásban |
| ESP-IDF 5.5.4 teljes P4 firmware, témacsomagok, OTA/USB csomagellenőrzés | Sikeres, ugyanabban a futásban |
| Windows-szimulátor | [Sikeres futás](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/39129299103) |

A transzfertesztek a tényleges kliens-/kiszolgálókódot futtatják 20 és 246 bájtos
fragmentumokkal, hibás CRC-vel, timeouttal, késői callbackkel és három külön
peerrel. A 288 pontos roundtrip minden bájtra hibainjektálást kap. Az NVS-tesztek
a szerepfelismerést, JSON-mentést és tárolási hibákat is ellenőrzik. A cache-tesztek
napváltást, masterváltást, újraindítást, elavulást és OTA-készültséget vizsgálnak.

A firmware mérete **0x2ce3d0 = 2 941 904 bájt**; az alkalmazáspartíció
**0x800000 = 8 MiB**, szabad **0x531c30 = 5 446 704 bájt (65%)**.
A bootloader **0x5ec0 bájt**, szabad **0x140 bájt (1%)**.
Ezek fordítási eredmények; fizikai rádiós vagy hosszú távú stabilitási mérést
nem helyettesítenek.

## Eszközös elfogadási próba

Az alábbi pontok eredménye ebben a jegyzetben még nincs hardveren igazolva:

- [ ] Masteren adat seed ki/be, PIN megjelenítése és párosítási ablak;
  hibás/lejárt PIN elutasítása, helyes PIN és újracsatlakozás új PIN nélkül.
- [ ] Energiaelőjelek, valódi nulla/hiányzó időjárás, óra, UTC-eltolás és
  nappali/éjszakai mód egyezése a két kijelzőn.
- [ ] SD-s Slave: mind a kilenc téma, saját SD-statisztika mentése és
  újraindítás utáni visszatöltése; a master múltjának automatikus átmásolása nélkül.
- [ ] SD nélküli Slave: indulás, csak Rendszer téma, belső beállítások és
  párosítás megőrzése; első szinkronig `--`, friss diagramok és tegnapi összehasonlítás.
- [ ] Kapcsolatkimaradás és visszatérés, megszakadt statisztikai transzfer,
  elavultság, napváltás; ismert master SD-hibája ne váltson szerepet.
- [ ] Wi-Fi/profil és auto-update be/ki öröklése, saját DHCP-cím, webfelület;
  éjféli frissítés és hibás próbaindítás visszaállítása mindkét Slave változatban.
- [ ] Több valós Slave, nagy SD-s témák és Wi-Fi együttes tartós futása,
  memória, hatótáv és megfelelő C6 Bluetooth HCI-firmware ellenőrzése.

Az eredményekhez a tesztelt firmware-verziót, P4/C6 hardvert és firmware-t,
az SD-s vagy SD nélküli módot, valamint az észlelt hibát érdemes rögzíteni.
PR-merge után a meglévő [release-folyamat](releases.md) készíti a kiadást;
egyszerű bezárás összevonás nélkül nem indít release-t.

## Folytatás a kis asztali kijelzővel

A hardver megérkezése után a ténylegesen összevont állapotból új ág induljon,
például `feature/desk-slave`. A pontos típust, panel-/érintésvezérlőt,
GPIO-kiosztást, flash/PSRAM méretet és P4 esetén C6-firmware-t előbb rögzíteni kell.

A javasolt felépítés ugyanebben a repóban külön, csak Slave-et tartalmazó
alkalmazás és hardverprofil, közös BLE-protokollal, párosítással, idő-/profil- és
statisztikaszinkronnal. A négyzetes felület külön elrendezést kap, saját PIN-paddal
és alapdiagnosztikával. A célonkénti build, projektazonosító, firmware-fájlnév és
OTA-kiválasztás különüljön el. A jelenlegi 10,1 colos image más kijelzőre nem való.

Ez fejlesztési irány, nem elkészült kis-kijelzős támogatás. A hardver beszerzéséig
sem új port, sem külön OTA-csatorna vagy új ág nem része ennek a lezárásnak.
