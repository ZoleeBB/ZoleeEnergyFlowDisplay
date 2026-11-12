# Fejlesztési napló és javítások

Az alábbi bejegyzések a korábbi fejlesztési köröket és kiadásokat foglalják össze; az aktuális használati leírások fent, a dokumentációs jegyzékben találhatók.

A bejegyzések a fejlesztéskori állapotot őrzik; az aktuális kiadásokhoz lásd a [Verziók](../../README.md#verziók) részt és a [GitHub Releases](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/releases) oldalt.

## Kaputelefon utáni témavisszatöltés (`fix/intercom-theme-restore`)

A csengetés megszűnése utáni kamera-/hangfeladat-leállás időzített naplózást kapott. A normál témák és a Loxone Otthon-oldalak külön betöltési időkorlátot használnak; a téma elakadásakor az ideiglenes **Rendszer** nézet nem írja felül a felhasználó mentett választását. A helyreállítás nem blokkoló katalóguszárolást használ, a tartós médiamegakadásnál kezdeményezett újraindítást pedig az OTA-műveletfoglalás és az SD-zár védi. Az új tesztek a teljes regressziós tesztcsomag részei.

**Állapot:** fejlesztési ág / PR; a teljes ESP-IDF- és CI-futtatás, illetve a valódi kijelzőn végzett memória- és újracsengési próba szükséges az összevonás előtt. Ez a bejegyzés nem jelöl ki önálló kiadási verziót. [Részletes működés](../intercom.md#kaputelefon-utáni-témahelyreállítás) · [Hibaelhárítás](../errors.md#kaputelefon-bezárása-után-beragadt-témabetöltés).

## Elektromos autó, Loxone-szerkesztő és újrarajzolás (`feature/electric-home`)

Az új `11-electric-car` téma 5-ös csomagverziója csak közvetlen Loxone módban
használható. A nappali és éjszakai háttérhez külön illesztett energiaút követi a
töltőkábelt. Az irányfüggő hálózati cím, az akkuhőmérséklet és az áttetsző fejléc
mellett a kártyán a mai autótöltés kWh-értéke és becsült napelemes százaléka is látszik.
A ház teljes fogyasztása továbbra is a `PowerFull`; az autó nincs kétszer elszámolva.
Hiányzó töltőadat nem válik nulla fogyasztássá, kimaradásra nincs energiabecslés.

A webes fogyasztó- és Otthon-kártyaszerkesztő kompakt mezőrácsot, nagyobb
ikonelőnézetet és magyar ABC-sorrendű ikonlistát kapott. A Nap pálya naphalója,
a Rendszer nézet energiaútjai és ikonjai csak változáskor frissülnek; a hostteszt
minden telepített témán nappal/éjjel és a Loxone-lapokon is ellenőrzi a nulla
üresjárati érvénytelenítést. Ez nem fizikai CPU-/FPS-mérés.

**Telepítés:** előbb a Master firmware, utána a `/templates` oldalon az
`11-electric-car.zip` (v5). A külön 720×720-as 4B Slave nem kap új autós oldalt.
[Használat és számítások](../electric-car.md) ·
[Ellenőrzési állapot](../electric-car-development.md) ·
[Újrarajzolási mérések](../performance.md#csak-változáskor-rajzoló-frissítés--master-és-4b-slave).

## Újdonság – 4B Slave: fényerő a Mastertől, OTA-visszaállás naplózása (`feature/slave-4b-brightness`)

| Funkció | Röviden |
| --- | --- |
| Fényerő SD nélkül | A Slave-nek nincs SD-kártyája és beállítóoldala, ezért a nappali/éjszakai fényerőt a védett profilban (változatlan hosszal és verzióval, a jelszó mögötti szabad bájtokban) a Mastertől kapja – a Beállítások oldalon átírt érték újraindítás nélkül érvényesül. [Részletek](../slave-waveshare-4b.md) |
| Nem ragad be a csatlakozás | Elmaradt CONNECT eseménynél a Slave eddig örökre „csatlakozás alatt" maradt (a Master címe és az RSSI felvillant, majd semmi). Most 15 másodperc után megszakítja és újra keres. |
| Párosítás nem vész el | A Slave eddig minden hitelesítési hibánál eldobta a párosítási kulcsot, így egy újrainduló Master után véglegesen leállt a kapcsolat. Mostantól csak a Master kulcshiány-jelzése (HCI 0x06) dobja el, és a kijelző kiírja, hogy újrapárosítás kell. |
| BLE-önjavítás | Ha a BLE host 90 mp alatt nem szinkronizál, vagy 10 percig nincs masterkapcsolat, a Slave magától újraindul (növekvő szünettel), és a napló rögzíti az okot. |
| Türelmesebb OTA | Ha a Slave frissítése közben a Master is újraindul, a hiányzó masterkapcsolat miatt az ellenőrzés 20 percig vár a visszaállás helyett. |
| OTA-visszaállás nyoma | Az ellenőrzési ablak 3-ról 5 percre nőtt (a Slave indulása lassabb); időtúllépéskor NVS-be kerül, melyik feltétel hiányzott, és a következő induláskor a napló kiírja a nem véglegesült verziót és az okot is. [Részletek](../ota.md) |

## Javítás – 4B Slave: néma jelenlétforrásnál nincs sötétítés, és nem fagyhat sötéten (`fix/slave-4b-night-blank`)

| Funkció | Röviden |
| --- | --- |
| Csak valódi 0-ra sötétít | Jelenlétforrással beállított profilnál a kijelző csak akkor sötétedik el, ha friss, érvényes jelenlétadat mondja, hogy nincs jelenlét. Néma vagy érvénytelen forrásnál (elmaradt BLE-olvasás, lejárt lease, Loxone-kiesés) a sötétítés fel van függesztve. [Részletek](../slave-waveshare-4b.md) |
| Nem marad sötét és néma | Ha a 33 ms-os kijelzőkör megáll, a fényerő és az érintés is halott: a főciklus 30 másodperc után újraindítja az eszközt, és a napló a következő induláskor megmondja az okot. |

## Javítás – 4B Slave: jelenlét alatt nem sötétedik el, teljes képernyős diagnosztika (`fix/slave-4b-presence-hold`)

| Funkció | Röviden |
| --- | --- |
| Csak valódi jelenlétvesztésre sötétít | A visszaszámlálás kizárólag friss, érvényes 0 jelenlétérték után indul. A kimaradt BLE-olvasás, a lejárt lease és a Master átmeneti kiesése nem jelenlétvesztés; 10 perc néma jel után évül el az utolsó ismert állapot. [Részletek](../slave-waveshare-4b.md) |
| Biztosabb ébresztés | Nulla fényerőről visszatéréskor a BSP háttérvilágítását is felkapcsoljuk, és a napló rögzíti az érintéseket, az elsötétülést és az ébredést. |
| Teljes képernyős diagnosztika | Firmware-verzió, üzemidő, tájolás, BLE/Wi-Fi/IP, napszak és fényerő, jelenlét- és sötétítésállapot, érintésszámláló, valamint a rendszernapló utolsó bejegyzései – görgethetően, „Bezárás" gombbal. |

## Újdonság – 4B Slave: nagyobb betűk mindenhol a fejlécen kívül (`feature/slave-4b-font-scale`)

| Funkció | Röviden |
| --- | --- |
| +20% betűméret | A fejlécen kívül minden felirat és érték egy mérettel nagyobb (16 → 20, 20 → 24, 28 → 34): csomópontnevek, teljesítményértékek, akkumulátor-részletek, állapotsor, diagnosztika, napi összesítő, PIN-pad és kaputelefon. [Részletek](../slave-waveshare-4b.md) |
| Új betűméretek | A 24 és 34 képpontos készlet ugyanabból a Noto Sans forrásból, a meglévő `tools/prepare_fonts.py`-vel készült; a Slave a 16 képpontosat már nem fordítja be. |
| Igazított elrendezés | A nagyobb sorokhoz a körök melletti és alatti feliratok helye is változott, így semmi nem lóg egymásba. |
| Javítás | A várakozó üzenetek „…" karaktere üres négyzetként jelent meg (nincs a betűkészletben); mostantól három pont, mint a fő kijelzőn. |

## Újdonság – 4B Slave: olvashatóbb felület és állítható tájolás (`feature/slave-4b-readability`)

| Funkció | Röviden |
| --- | --- |
| Vastagabb energiautak | Az energiautak, a körök és a mozgó pontok kétszer olyan vastagok, így a háttérkép felett is jól követhetők. |
| Félkövér feliratok | A Hálózat/Akkumulátor/Napelem/Ház nevek és a teljesítményértékek szintetikus félkövérrel rajzolódnak – új font nélkül, változatlan elrendezésben. |
| Egysoros fejléc | Az időjárás ikon és a hőmérséklet rögtön a „Zolee Energy" felirat után áll, a felszabadult helyre a páratartalom került az ikonjával. |
| Állítható tájolás | A Master `/loxone` oldalán, a Slave kártyáján választható 0/90/180/270°. A beállítás a jelenlétprofillal megy át BLE-n, a Slave elmenti és újraindul; az érintés is forog. [Részletek](../slave-waveshare-4b.md) |
| Slave IP-cím | A 4B diagnosztikai rétege kiírja a Wi-Fi IP-címet is. |
| Frissítés a gombról | A Master „frissítések keresése" gombja a párosított Slave-eket is megkéri, hogy nézzék meg a saját kiadásukat (a napváltáskori automatikus ellenőrzést a Slave eddig is futtatta). |

## Javítás – a 4B Slave régebbi szilíciumon is települ (`fix/slave-4b-chip-revision`)

| Funkció | Röviden |
| --- | --- |
| 4B telepítés v1.x lapkán | A megérkezett 4B panel ESP32-P4 **v1.3** szilíciumú, a Slave csomag viszont v3.0+-t kért: a feltöltés `requires chip revision in range v3.0 - v3.99` hibával állt meg. A Slave firmware mostantól v1.0 – v1.99 lapkára készül (a PSRAM marad 200 MHz). A két revíziócsalád az ESP-IDF-ben kizárja egymást, a Master 10.1 változatlanul v3.0 – v3.99. [Részletek](../usb-install.md) |
| Ellenőrzött csomag | A kiadás csomagolása kiolvassa a bootloader és az alkalmazás fejlécéből a revíziókövetelményt, és eltérésnél nem enged ki csomagot. Így nem maradhat észrevétlen, ha egy build rossz szilíciumra készül. |
| Célzott hibaüzenet | A `flash.py` a valódi hibához igazítja a tanácsot: revízióhibánál nem a kézi BOOT módot ajánlja (az nem segít), hanem megmondja, mi a baj. |

## Újdonság – visszakereshető eseménynapló (`feature/event-log-history`)

| Funkció | Röviden |
| --- | --- |
| Napló az SD-n | A RAM-napló bejegyzései kötegelve az SD-re is kikerülnek (`/sdcard/logs`), így újraindítás után is látszik, mi történt előtte. Mindig a mai és az előző napi fájl marad meg, napi 512 KiB felső korláttal. [Részletek](../errors.md) |
| Nem akad össze semmivel | A kiírás a tárzárat csak nem blokkolóan kéri: ha a kijelző témát tölt vagy telepítés fut, kimarad, és a következő körben újrapróbálja. Az esemény rögzítése továbbra is tisztán RAM-művelet, bármelyik feladatból hívható. |
| Görgethető eseménylista | Az információs oldal „Legutóbbi események" kártyája fix magasságú és görgethető, a legfrissebb eseménnyel az élen – ahogy a kijelző diagnosztikai nézete is mutatja. Mellette a mai és az előző napi teljes napló is megnyitható, szintén csökkenő időrendben. |

## Javítások – témabetöltés és segédfájlok (`fix/theme-load-starvation`)

| Funkció | Röviden |
| --- | --- |
| Nem akad be a kijelző frissítés után | A segédfájlokat készítő háttérmunka nem tartja fél percig az SD-zárat: a dekódolás alatt elengedi, 64 KiB-os blokkokban ír, és félreáll, ha a kijelzőnek kell a kártya. A betöltés-figyelő pedig vár, ha a betöltés csak sorban áll – nem vált tartalék nézetre. [Részletek](../errors.md) |
| Megmarad a témaválasztás | Egy sikertelen betöltés nem írja át a mentett témát: a kijelző ideiglenesen a Rendszer nézetet mutatja, a választás érintetlen. |
| Nem tűnik el a téma a listából | Átmeneti olvasási vagy foglalási hiba nem dobja ki a témát a katalógusból („nincs telepítve”); a lista megtartja a bejegyzést, és a következő vizsgálat újrapróbálja. |
| Megmaradnak a segédfájlok | Frissítéskor a változatlan képek előre dekódolt segédfájljai átkerülnek az új könyvtárba, ha azt a méretet az új leíró is használja: a hátterek nem készülnek újra, a téma a frissítés után is azonnal gyorsan tölt. A ZIP-es kézi telepítésre és a GitHubos frissítésre egyaránt él. |
| Hamarabb készülnek a segédfájlok | Először az aktív téma hátterei; a háttérmunka foglalt zár után 2,5 s-ot vár, nem egy percet. [Részletek](../performance.md) |
| Következetes sablonkezelő | Az előnézetek betöltése alatt a soronkénti frissítés is zárva van. A zár a haladást figyeli: minden beolvasott előnézet kitolja a határidőt, így a teljes, eszközön 30–60 másodperces olvasás alatt zárva marad, és csak akkor oldódik fel, ha elfogytak a képek vagy 20 másodpercig nincs haladás – az „Összes frissítése” nem marad tiltva, de korán sem nyílik meg. |

## Javítások – GitHub-frissítés és memória (`fix/template-update`)

> **A v3.1.4 kiadást ne telepítsd.** A benne lévő kép-segédfájl feladat 4 KiB-os
> verme túlcsordul, és a készülék indulás közben újraindul. Másik OTA-partícióval
> a rendszer magától visszaáll, friss USB-telepítés után viszont csak újabb
> USB-telepítés segít. A javítás ebben a körben van; a következő kiadás már jó.

| Funkció | Röviden |
| --- | --- |
| Működő GitHub-frissítés | Az OTA és a gyári témák frissítése ismét lefut. A hibák okát a visszajelző sáv és az eseménynapló is megmutatja (kapcsolódás, HTTP-állapot, kéréskorlát, memória), kapcsolódási hibánál pedig kétszer újrapróbálkozik. [Részletek](../automatic-update-diagnostics.md) |
| Stabil indulás | A kép-segédfájlokat előállító háttérfeladat verme túlcsordult, és az eszköz bootciklusba került. Az útvonalpufferek a kupacba kerültek, a feladat pedig naplózza a szabad vermét. [Részletek](../errors.md) |
| Gyorsabb témaváltás | Az SD-olvasás egyetlen, induláskor foglalt belső DMA-pufferen megy: nem függ a pillanatnyi memóriahelyzettől, és elmaradnak a megszakadt betöltések. |
| Háttérfeladat-képernyő | Frissítés és telepítés alatt a kijelző statikus „Háttérfeladat miatt foglalt…” üzenetet mutat, így a mag, az SD és a hálózat a műveleté. A csengetés ilyenkor is elsőbbséget kap. |
| Memóriadiagnosztika | A Diagnosztika oldal alján a belső heap és feladatonként a szabad verem is látszik; a `/api/diagnostics/tasks` a régiós bontást a soros konzolra is kiírja. [Részletek](../performance.md) |
| SD-hiba nem zár ki | Hibás kártyánál is lefut a firmware aktiválása és a webes újraindítás; a sikertelen statisztikamentés csak naplóba kerül. |
| 90°-os tájolás | Megszűnt a 15 másodperces próbaidőszak: indulás közbeni újraindítás már nem állítja vissza 270°-ra. [Részletek](../configuration.md) |
| Rendezett sablonkezelő | Az előnézetek betöltése alatt a telepítés és a frissítéskeresés gombjai zárva; a keresés mindig üres listával indul. |
| Azonosítható tesztcsomag | A CI-buildek verziója a commitot is tartalmazza (`v0.0.0-ci.g<commit>`), így az információs oldalon látszik, melyik fut. [Részletek](../releases.md) |

## Újdonságok – felhasználói élmény (`feature/ux-improvements`)

| Funkció | Röviden |
| --- | --- |
| Akku hátralévő idő | Az Akku diagnosztika témán: „Kb. 1 óra 40 perc múlva tele”, „Még kb. 6 óra 20 percig bírja (15%-ig)”; szigetüzemben és áramszünetre készülve kiemelve. Alsó határ beállítás nélkül normál üzemben 15%, áramszünetnél és szigetüzemben 5%. [Részletek](../battery-diagnostics.md#hátralévő-idő) |
| Jelenlétvezérelt Master | Loxone módban opcionális *Jelenlét* adat: éjjel 0 → teljes sötét, érintés vagy 1 → éjszakai fényerő. Párosítás nélkül minden a régi. [Részletek](../loxone-direct.md#jelenlét-és-postaláda-a-master-kijelzőn) |
| Postaláda-jelzés | Loxone *Posta érkezett* (0/1) állapot: 1-nél postaláda-ikon minden téma fejlécében. |
| 4B Mai összesítő | A 4B Slave-en vízszintes húzással napi termelés, fogyasztás, hálózat, akku és pénzügy. [Részletek](../slave-waveshare-4b.md#mai-összesítő) |
| Mentés és visszaállítás | Beállítások letöltése/visszatöltése egy fájlban, belső flash-tartalék és automatikus visszaállítás új SD-kártyára. [Részletek](../backup.md) |
| Témafrissítés GitHubról | A Sablonok oldalon jelzi az elavult gyári témákat, és egy kattintással frissíti őket. [Részletek](../templates.md#gyári-témák-frissítése-a-githubról) |
| Gyorsabb témaváltás | Natív 1280×800-as hátterek, valamint telepítés után a háttérben elkészülő, előre dekódolt kép-segédfájlok: a felépítésből és az első képkockából kimarad a PNG-dekódolás és az LVGL-méretezés. [Részletek](../performance.md) |
| Mért témaváltás | Minden váltás egy sorban naplózza a fázisokat (betöltőkép, SD, csere, felépítés, első képkocka) a webes eseménynaplóba is. [Részletek](../performance.md#témaváltás-fázisidői-a-valódi-panelen) |
| Megbízhatóbb lapozás | A betöltés közbeni húzás sem vész el, a sötét/világos háttér váltáskor is betöltődik, elakadt betöltésből a kijelző magától visszatér. [Részletek](../gestures.md) |
| Új webes felület | Egységes, világos/sötét megjelenés, felső menü minden oldalon, mobilbarát elrendezés; a Loxone oldal zöld témával és áttekinthetőbb fogyasztó-/kártyaszerkesztővel. |
| Egyszerűbb Loxone párosítás | Az időjárás, a nappálya, a jelenlét és a posta sorában nincs szorzó és eltolás; az SD foglaltság két tizedesre látszik. [Részletek](../loxone-direct.md) |
| Kezelési útmutató | Minden gesztus és érintés egy helyen: [Kezelés és gesztusok](../gestures.md) |

A Master és a 4B Slave firmware-ét is frissíteni kell. A firmware után érdemes a
Sablonok oldalon az **Összes frissítése** gombbal letölteni a gyári témák új
verzióit: az Akku diagnosztika hátralévő idő mezője és a natív hátterek ezekben
érkeznek. A frissítés után a kijelző a háttérben, fájlonként elkészíti az előre
dekódolt kép-segédfájlokat; ettől a témaváltás érezhetően gyorsabb lesz.

## Párosított 4B kaputelefon és csengőhangok – PR #23

A Masteren mentett kaputelefont a párosított 720×720-as Waveshare 4B Slave-ek
is használják, külön Slave-konfiguráció és SD-kártya nélkül. A kamera adatai
védett BLE-profilon érkeznek; a videó és hang közvetlen Wi-Fi-kapcsolat.
A Master egyetlen kijelzőnek enged beszélgetést, így nincs párhuzamos SIP-hívás.
Kapunyitás minden kijelzőn elnémítja a csengőhangot, az aktív beszélgetés
5 másodperc után bontódik. A Master és a Slave firmware-ét is frissíteni kell.

A Master Loxone-oldalán legfeljebb **8 MiB ZIP és 32 hang** tölthető fel:
**PCM WAV, 16 bit, mono, 16 kHz, hangonként legfeljebb 60 másodperc**.
A feltöltés először törli a teljes korábbi hangkészletet, majd egyenként
ellenőrzi és menti a fájlokat. A hibás fájlt kihagyja, a végén névvel és
indokkal felsorolja; megszakításkor a régi csomag nem áll vissza.
A választott hang és az ismétlés külön menthető. A Slave a hangot a Mastertől
kapja, a lejátszás RAM-ból történik. A repóban tárolt készlet jelenleg 18 hangot tartalmaz; a kiadási ZIP és az
USB-kezdőkészlet a `ringtone.json` teljes listájából készül. A hangok eredeti
forrás- és licencadatai a manifestben változatlanul megmaradnak.

A kiadás külön `-ringtones.zip` és `-templates.zip` mellékletet is készít,
SHA-256 összegekkel; a Master USB-csomagjában SD-kezdőkészlet található.
Az eszköz főoldalának alján akkumulátor-statisztika és megerősítést kérő
nullázás érhető el: a napi grafikonok és a névleges kapacitás megmaradnak.

[Master/Slave működés és telepítés](../slave-intercom.md) ·
[SD- és feltöltési működés](../intercom.md#csengőhang-betöltés-és-csomagcsere) ·
[Akkumulátor-statisztika és nullázás](../battery-diagnostics.md#webes-akkumulátor-statisztika-és-nullázás) ·
[PR #23 átadási állapot és ellenőrzések](../slave-intercom-handover.md).
A #23 összevonása a normál patch-kiadási szabályt használja;
a korábbi #20 egyszeri v3.1.0 szabálya erre nem érvényes.
