<h1 align="center"><img src="assets/branding/readme-banner.png" alt="Zolee Energy Flow Display" width="1600"></h1>

<p align="center">
  <a href="https://ko-fi.com/E1H8299KGN"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Ko-fi támogatás" height="36"></a>
  <a href="https://revolut.me/zoleebb"><img src="assets/branding/revolut-support.svg" alt="Revolut-me támogatás" height="36"></a>
</p>

Otthoni energiaáramlás-kijelző a **Waveshare ESP32-P4-WIFI6-Touch-LCD-10.1 (1280×800)**
panelhez, külön **Waveshare 4B Slave (720×720)** firmware-rel. A napelem, a ház,
a hálózat és az akkumulátor pillanatnyi adatait, energiaútjait és statisztikáit
mutatja, nappali és éjszakai megjelenéssel.

Adatfogadás: **MQTT, HTTP POST, közvetlen Loxone vagy Bluetooth Slave**.
A beállítás, frissítés és diagnosztika helyi webfelületen érhető el.

[Első indítás](#első-indítás) · [Funkciók](#funkciók-és-üzemmódok) ·
[Téma-előnézetek](#téma-előnézetek) · [Dokumentáció](#dokumentáció) · [Verziók](#verziók)

## Első indítás

1. Telepítsd a megfelelő Master vagy Slave firmware-t az [USB útmutató](docs/usb-install.md) szerint.
2. Készítsd elő a [konfigurációt](docs/configuration.md) és az [SD-kártyát](docs/sd-card.md), amennyiben az adott üzemmód használja.
3. Állítsd be a kiválasztott [adatkapcsolatot](docs/integrations/README.md); Slave esetén végezd el a Masterrel a párosítást.
4. A kezeléshez és a helyi webfelülethez kövesd az [első indítás részletes leírását](docs/first-start.md) és a [gesztusok útmutatóját](docs/gestures.md).

## Funkciók és üzemmódok

- **Energiaáramlás és statisztika:** pillanatnyi teljesítmények, animált energiautak, napi/heti/havi adatok és pénzügyi mutatók. [Statisztika](docs/statistics.md).
- **Adatkapcsolatok:** MQTT, HTTP POST, közvetlen Loxone és Bluetooth Master/Slave. [Beállítások](docs/integrations/README.md).
- **Loxone Otthon:** külön mért fogyasztók, lapozható kapcsoló-, nyomógomb-, szenzor- és állapotkártyák. [Otthon-konfiguráció](docs/v3-home-config.md).
- **Világításvezérlés:** automatikus blokkfelismerés, `− / Ki / +` gombok, dimmer- és RGB/RGBW-kezelés. [Útmutató és képek](docs/lighting-controller.md).
- **IRC termosztát:** célhőfok a kötött termosztátikonon, mért szobahőmérséklet, fűtési/hűtési üzemmódjel és `− / +` állítás 0,5 °C-os lépéssel. [Használat és beállítás](docs/irc-controller.md).
- **Redőnyvezérlés:** automatikus és központi árnyékolás fix redőnyikonnal, `Fel / Stop / Le` gombokkal; egyedi redőnynél külön százalékos beállítóablak. [Használat és beállítás](docs/shading-controller.md).
- **Kaputelefon:** élőkép, kétirányú hang, kapunyitás és csengőhangok, Master és 4B Slave támogatással. [Útmutató](docs/intercom.md).
- **Energiafunkciók:** [akkudiagnosztika](docs/battery-diagnostics.md), [elektromos autó](docs/electric-car.md) és [helyi energiaasszisztens](docs/energy-assistant.md).
- **Megjelenítés:** világos/sötét témák, saját sablonok és [Loxone Otthon kaputelefon-kijelzőként](docs/energy-flow-disable.md).
- **Karbantartás:** webes beállítások, mentés/visszaállítás, OTA és diagnosztika. [Útmutatók](docs/maintenance/README.md).

## Téma-előnézetek

A képek a valódi LVGL-rajzolóból készültek. A Loxone-oldalak és az Elektromos autó téma csak Masteren, közvetlen Loxone módban érhetők el.

| Téma | Nappal | Éjjel |
| --- | --- | --- |
| Rendszer (beépített) | ![Nappal](assets/previews/builtin-light.png) | ![Éjjel](assets/previews/builtin-dark.png) |
| Loxone – Ház külön mért fogyasztói | ![Fogyasztók nappal](assets/loxone-home/previews/consumers-light.png) | ![Fogyasztók éjjel](assets/loxone-home/previews/consumers-dark.png) |
| Loxone – Otthon-kártyák | ![Otthon-kártyák nappal](assets/loxone-home/previews/cards-light.png) | ![Otthon-kártyák éjjel](assets/loxone-home/previews/cards-dark.png) |
| Élő otthon | ![Nappal](templates/01-live-home/preview-light.png) | ![Éjjel](templates/01-live-home/preview-dark.png) |
| Elektromos autó (Loxone) | ![Nappal](templates/11-electric-car/preview-light.png) | ![Éjjel](templates/11-electric-car/preview-dark.png) |
| Energiautak | ![Nappal](templates/02-energy-paths/preview-light.png) | ![Éjjel](templates/02-energy-paths/preview-dark.png) |
| Műszerfal | ![Nappal](templates/03-instruments/preview-light.png) | ![Éjjel](templates/03-instruments/preview-dark.png) |
| Napi mérleg | ![Nappal](templates/04-daily-balance/preview-light.png) | ![Éjjel](templates/04-daily-balance/preview-dark.png) |
| Nyugodt otthon | ![Nappal](templates/05-calm-home/preview-light.png) | ![Éjjel](templates/05-calm-home/preview-dark.png) |
| Energiakör | ![Nappal](templates/06-energy-orbit/preview-light.png) | ![Éjjel](templates/06-energy-orbit/preview-dark.png) |
| Energiafőkönyv | ![Nappal](templates/07-energy-ledger/preview-light.png) | ![Éjjel](templates/07-energy-ledger/preview-dark.png) |
| Energiastatisztika | ![Nappal](templates/08-statistics/preview-light.png) | ![Éjjel](templates/08-statistics/preview-dark.png) |
| Nap pálya | ![Nappal](templates/09-solar-journey/preview-light.png) | ![Éjjel](templates/09-solar-journey/preview-dark.png) |
| Akku diagnosztika | ![Nappal](templates/10-battery-diagnostics/preview-light.png) | ![Éjjel](templates/10-battery-diagnostics/preview-dark.png) |

A webes sablonkezelő a csomagok `preview-light.png` / `preview-dark.png` képeit mutatja; régi csomagnál üres keret marad, amíg a témát ZIP-ből frissíted.

További képi ellenőrzések: [beépített ikonok](docs/previews/icons.png), [rendszernézet lapjelzővel](docs/previews/builtin-pagination.png), [közös diagnosztika](docs/previews/diagnostics.png).

## Dokumentáció

A teljes anyag a **[dokumentációs kezdőlapon](docs/README.md)** érhető el,
témakörönként csoportosítva.

| Témakör | Tartalom |
| --- | --- |
| [Telepítés és használat](docs/getting-started/README.md) | Első beüzemelés, hardver, SD-kártya és napi kezelés. |
| [Funkciók és vezérlés](docs/features/README.md) | Energiastatisztika, Loxone Otthon, világítás, kaputelefon és kiegészítő funkciók. |
| [Adatkapcsolatok és Master/Slave](docs/integrations/README.md) | Adatprotokollok, mintakonfigurációk, közvetlen Loxone-kapcsolat és Bluetooth. |
| [Témák és megjelenítés](docs/themes/README.md) | Téma-előnézetek, telepítés, saját sablonok, ikonok és képek. |
| [Karbantartás és hibakeresés](docs/maintenance/README.md) | Frissítés, mentés, diagnosztika, ismert hibák és támogatás. |
| [Fejlesztés és ellenőrzés](docs/development/README.md) | Felépítés, build, szimulátor, kiadási folyamat, memória- és veremvédelem. |
| [Fejlesztési tervek és átadások](docs/history/README.md) | Dátumhoz vagy fejlesztési fázishoz kötött tervek, tapasztalatok és átvételi jegyzetek. |

## Verziók

[Kiadási leírások és fejlesztési napló](docs/versions/README.md) ·
[Letölthető kiadások](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases)
