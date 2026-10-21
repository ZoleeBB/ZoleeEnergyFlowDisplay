# v3 – telepítési és eszközös átadási lista

A fejlesztési ág: `feature/v3-loxone-home`. Ez fejlesztői tesztverzió,
nem publikált v3.0.0 kiadás. A szoftveres ellenőrzések eredménye a
[folytatási tervben](v3-development-plan.md) szerepel, pontos forráscommitonként.
Az alábbi eszközpróbák eredménye még nincs igazolva.

## Telepítési sorrend

1. A sikeres **Waveshare 10.1 Master** futásból töltsd le a
   `firmware-packages-<commit>` és `template-packages-<commit>` artifactot.
   A két csomag azonos forráscommithoz tartozzon. Az új javításhoz ne használj
   korábbi, zöld futásból származó binárist.
2. A megfelelő eszköz `/update` oldalán telepítsd a saját OTA-fájlját:
   Master: `ZoleeEnergyFlowDisplay-…-ota.bin`; 4B:
   `ZoleeEnergyFlowDisplay-slave-4b-…-ota.bin`. A két fájl nem cserélhető fel.
   Első USB-telepítéshez ugyanitt van mindkét teljes USB ZIP.
3. Masteren a `/templates` oldalon telepítsd újra a `loxone-home.zip` fájlt,
   majd az új `05-calm-home.zip` és `06-energy-orbit.zip` csomagot.
   Azonos témaazonosító frissítése megtartja a sorrendet. Az OTA önmagában
   nem cseréli le az SD-kártyán levő képeket és témaleírókat.
4. A közvetlen Loxone kapcsolatot és a kártyákat a `/loxone` oldalon állítsd be.
   Ellenőrizd a kiválasztott állapotot, a W → kW szorzót és kapcsolónál a saját
   `active` visszajelzést. Az indításjavaslat fogyasztónként külön bekapcsolható.
5. A Slave jelenlétfunkciójához mindkét eszköz friss firmware-e kell.
   Párosítás után a Master Loxone-oldalán nevezd el a kijelzőt, válassz
   jelenlétforrást és időkorlátot; a sötétítés alapból kikapcsolt.
   Várd meg a tartós mentés visszajelzését.

A közös Loxone-csomag nélkül a szerkesztő pirosan figyelmeztet, ikonelőnézet
nem jelenik meg, a két speciális kijelzőoldal nem nyitható meg. A
[mintaprofil](../examples/loxone-home.sample.json) szerkezetet mutat;
szemléltető UUID-it saját Loxone-állapotokra kell cserélni. Nem importálható
`config.json`-ként. [Részletes formátum és kézi SD-használat](v3-home-config.md).

A panelvillanás F11 javításához az `adfa395` vagy az azt követő forrásból
készült Master firmware kell. A már telepített v3 `loxone-home.zip` ehhez
változatlanul használható; új grafikai csomag nem része ennek a javításnak.
A teljes képkockás mód miatt a fényképes témák és az érintés sebességét is
ellenőrizd, tartós Wi-Fi/BLE forgalom mellett.

## Melyik eszközön mi működik?

| Funkció | Master, Loxone | Master, HTTP/MQTT | 10.1/4B Slave |
| --- | --- | --- | --- |
| Fogyasztói és elnevezett Otthon-oldalak | Igen, SD-csomaggal | Nincs | Nincs |
| Kapcsoló és pillanatnyi gomb | Igen, támogatott Loxone-objektumokra | Nincs | Nincs |
| Energiaasszisztens | Igen, csak javaslat | Nincs | Nincs |
| Slave jelenlétprofil szerkesztése | Igen, párosított kijelzőnként | Nincs | A Mastertől kapja |
| Nappali/éjszakai saját fényerő | Meglévő viselkedés | Meglévő viselkedés | Igen |
| Éjjeli sötétítés és jelenlét/érintés ébresztés | Profilt továbbít | Kikapcsolt profilt továbbít | Friss Loxone-Master profillal |

## A felhasználói próba utáni javítások ellenőrzése

Mindegyik sor fizikai eszközön végrehajtandó. A kapcsolás eredményét a
Miniserver tényleges állapotával is vesd össze. Az impulzushoz olyan
tesztobjektumot válassz, amelynek egyszeri működése egyértelműen látható.

| Azonosító | Próba | Elvárt eredmény |
| --- | --- | --- |
| F01 | Közös csomag telepítése, világos/sötét előnézet | Mindkét kép megjelenik a sablonkezelőben |
| F02 | Ikonváltás, majd csomag nélküli szerkesztő | Telepítve kis kép; csomag nélkül felül piros figyelmeztetés |
| F03 | Kapcsoló BE/KI és egy impulzus; kapcsolatkimaradás | Valós állapot-visszajelzés; egyszeri impulzus; visszakapcsolódáskor nincs ismételt parancs |
| F04 | Otthon, fogyasztók és diagnosztika bezárása | Látható betöltőkép, majd pontosan az előző téma |
| F05 | Fogyasztók nyitása és legalább egy perc nyugalmi nézet | Saját háttér; nincs villogás sem betöltéskor, sem utána |
| F06 | Rendszer energiacsillagának házképe közepére érintés | A fogyasztói oldal nyílik meg |
| F07 | Bluetooth-párosítási ablak megnyitása/lejárata | Ikon látszik, felirat két tengelyen középen; a státusz követi az ablakot |
| F08 | Betöltés magyar és angol nyelven | „Adatok és grafikai elemek betöltése...” / “Loading data and graphics...” |
| F09 | Nagy téma törlése és a lista későbbi frissítése | Azonnali forgó kör, várakozási jelzés; siker vagy hiba után kezelhető lista |
| F10 | Az új commit három sikeres Actions-futásának artifactjai | Összesen négy csomag, az alábbi tartalommal |
| F11 | 09-es témáról fogyasztók: betöltés, nappal/éjjel, 10–12 elem, nulla fogyasztású TV/klíma; vissza és újra nyitás | Fényképes háttér és minden ikon látszik; nincs kékes villanás; az érintés/lapozás folyamatosan használható |

A négy kimenet: közös firmware-csomag, témacsomagok, fő Windows-szimulátor,
4B Windows-szimulátor. Külön OTA-másolat és letölthető Linux-szimulátor nincs.
A Linux-, LVGL- és böngészőtesztek ettől tovább futnak.
[Részletes tartalom és kiadási függőségek](releases.md).

## V3 funkciók és stabilitás

- [ ] Hét fogyasztó: felül négy, alattuk három középen; nulla szürke ikonnal,
  hiányzó mérés külön jelzéssel, 1 kW alatt W. Az X az előző témára tér vissza.
- [ ] Legalább két elnevezett Otthon-lap és egy üres lap: cím, pontok és
  mindkét lapozási irány helyes. Gombról indított húzás nem küld parancsot.
- [ ] Webes szerkesztés közben két háttérfrissítés, szűrés és mentés:
  a helyi szöveg és párosítás megmarad, hibás SD-mentés nem jelez sikert.
- [ ] Slave nappal jelenlét nélkül is a saját nappali fényerején marad.
  Éjjel elsötétül, rövid jelenlétimpulzus és tartós aktív jel is ébreszti;
  az utolsó jel után a beállított idő végén újra sötét.
- [ ] A sötét Slave első érintése, mozgása és felengedése csak ébreszt;
  a következő érintés használható. PIN-bevitelkor látható marad.
- [ ] Master/Slave újraindítás után a név, profil és saját fényerő megmarad;
  lecsatlakozás és visszatérés nem játszik vissza régi jelenlétimpulzust.
- [ ] Energiaasszisztens: 100% alatti akkunál, akkuból exportálásnál,
  már működő fogyasztónál vagy elégtelen többletnél nincs indításjavaslat.
  A megfelelő állapot kétperces fennállása után javasol; X nem kapcsol eszközt.
  [Pontos feltételek és éjszakai referencia](energy-assistant.md).
- [ ] A magasabb éjszakai fogyasztás jelzése csak legalább három igazolt,
  újonnan mért összehasonlítható éjszaka után vizsgálható. Régi fájlból nincs
  kitalált referencia; az energiaelőzmény megmarad.
- [ ] A 05/06 téma nappal/éjjel és HU/EN nyelven helyesen jelenik meg.
  Az Energiakör idővonala éjfélnél, napkeltekor és napnyugtakor is helyes;
  hiányzó napadatból nem rajzol kitalált nappali szakaszt.
- [ ] HTTP/MQTT adatfogadás, korábbi témák, statisztika, OTA és BLE energia-
  adatmegosztás tovább működik; az új Loxone-oldalak csak a megfelelő módban élnek.

Rögzíts indulás után, majd legalább 100 téma-/oldalváltás és tartós
Wi-Fi/BLE/adatforgalom után egy diagnosztikai pillanatképet. Meglévő mezők:
`free_internal_kib`, `free_psram_kib`, `largest_dma_block_bytes`, CPU0/CPU1,
`uptime_seconds`, adatkapcsolat és SD-állapot. A megmaradt összes memória
önmagában nem bizonyítja a DMA-blokk vagy az animáció megfelelő működését.
Figyeld az újraindulást, az SD-/foglalási hibákat és a kép egyenletességét is.
Az FPS-t a jelenlegi diagnosztika nem méri; a szimulátor FPS-e nem panelmérés.

| Rögzítendő adat | Eredmény |
| --- | --- |
| Forráscommit, artifact és OTA SHA256 | Kitöltendő |
| Master/Slave hardver, P4/C6 firmware, SD típusa | Kitöltendő |
| HU/EN, nappal/éjjel, profil és beállított időkorlát | Kitöltendő |
| Indulási és terhelés utáni RAM/PSRAM/DMA/CPU | Kitöltendő |
| F01–F11 és funkciópróbák eredménye, eltérés | Kitöltendő |

## Kiadási állapot

A fizikai próbák és az új forrás teljes firmware-/csomagellenőrzése nyitott
átadási pontok. PR csak a vállalt feladatok lezárása után készül. A v3.0.0
verzióválasztás kizárólag e repó saját `feature/v3-loxone-home` ágának igazolt
`main` összevonására érvényes; más ág és fork nem kap ilyen főverzió-emelést.
A szabály és az újrafuttatás tesztelt, de nincs előre létrehozott v3 tag vagy release.

## English quick start and acceptance

This is the v3 development branch, not a published v3.0.0 release. Use firmware
and template artifacts from the same successful source commit. Install the
correct OTA image on each board, then update `loxone-home.zip`,
`05-calm-home.zip` and `06-energy-orbit.zip` on the Master's template manager.
OTA does not replace SD templates. Configure consumers and named Home pages
on `/loxone`; the JSON example is a separate Home profile, not `config.json`.

Only the Master in Loxone mode exposes consumers, Home controls and the local
energy assistant. A switch needs its own `active` feedback state; a pulse is
sent once and is never automatically replayed after reconnecting. The stored
control identity is resolved to the live Loxone `uuidAction` command address.

For presence wake-up, update both Master and Slave. Pair the display, choose
its presence source and timeout, enable night blanking explicitly, and wait
for persistent-save confirmation. Daytime uses the Slave's own day brightness;
night-time wake-up uses its own night brightness. The first touch only wakes
the screen, including movement and release. Old Masters keep legacy brightness.

On physical hardware, verify both control types, all three close/loading paths,
consumer background and flicker, the built-in house hit area, icon previews,
the missing-package warning, Bluetooth label alignment and delete progress.
Also test page swipes without commands, reboot persistence, disconnect recovery,
100 page/theme transitions and sustained Wi-Fi/BLE traffic. Record source SHA,
board/firmware versions, internal RAM, PSRAM, largest DMA block, CPU and errors.
Simulator success does not establish physical panel, radio or memory behavior.
Hardware acceptance remains open. The tested v3.0.0 selection rule applies only
to a verified merge of this repository's own `feature/v3-loxone-home` branch;
no v3 tag or release has been created.

F11 panel follow-up: use Master firmware from `adfa395` or a later source.
The existing v3 Home resource ZIP remains valid. From theme 09, open consumers,
check the loading screen and day/night pages with 10–12 consumers, including
zero-power TV/AC icons, then close and reopen. The photo and every icon must
remain visible without cyan flashes. Check touch responsiveness and animated
photo themes under Wi-Fi/BLE traffic with the complete-frame display mode.
