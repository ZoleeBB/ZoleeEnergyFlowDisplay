# PR #23 – Slave kaputelefon és csengőhangok átadása

Ág: `feature/slave-intercom-ringtones`, cél: `main`.
[PR #23](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/pull/23).
Állapot: 2026-10-25. A felhasználó a csengőhang-/SD-/feltöltési javítás után
megfelelő működést jelzett. Az összevonást a felhasználó végzi.

## Végleges működés

- A Master és a párosított 4B Slave ugyanazt a kaputelefont használja;
  egyetlen központi engedélyezett hanghívás lehet. BLE-kiesés és régi engedély
  nem hagyhat nyitva mikrofont. A médiaút, az AEC és a kapunyitás szabályai
  a naplótisztításkor változatlanok.
- A hangcsomag legfeljebb 8 MiB ZIP és 32 PCM16/mono/16 kHz WAV;
  egy hang legfeljebb 60 másodperc. A nyilvános kezdőkészlet egy CC0 hang.
  A privát, 18 WAV-os csomag nem része a nyilvános repónak.
- A feltöltés először törli a teljes régi hangkönyvtárat és a félbemaradt
  `.new`/`.old` készletet. Ezután fogadja a ZIP-et, majd egyenként ellenőrzi
  és menti a hangokat. Hibánál kihagy és folytat; a végén fájlnév és ok látható.
  Nincs visszaállás a régi csomagra. A csengőhangválasztás külön menthető.
- A hangminta a böngészőben szól; a csengetés a mentett hangot játssza a
  kijelzőn. A Slave SD nélkül, a Mastertől kapja a hangot.
- Az eszköz főoldalának alján akkumulátor-statisztika és tartós nullázás van.
  A névleges kapacitás és a napi energiagrafikonok nullázáskor megmaradnak.

## Mi okozta a feltöltési és SD-tüneteket?

A kódban igazolható problémák a fájlhozzáférési és csomagtelepítési útvonalon
voltak, nem egy megváltoztatott SD-driverben:

1. A korábbi fogadó a hálózati ZIP-fogadás teljes idejére fogta a közös SD-zárat.
   Lassú vagy megszakadt küldés közben a többi SD-művelet várakozott.
   Az új hangfeltöltő minden darabot megnyit/ír/lezár; hálózati fogadáskor
   nincs nyitott feltöltési fájl és nincs SD-zár.
2. A korábbi telepítő teljes készletet ellenőrzött és könyvtárátnevezéssel
   cserélt. Egy hibás elem elutasította az egész csomagot; a régi vagy
   félbemaradt készlet helyreállítása is része volt az útvonalnak.
   Az új folyamat törléssel indul és önálló fájlhibákkal dolgozik.
3. A téma- és hangkibontás közös, kibővített kódban élt. A témák feltöltőjét
   és kibontóját a működő `main` változatra állítottuk vissza; a hangnak saját
   kibontója van. A hangolvasás pufferelt stdio, saját DMA-puffer és `setvbuf`
   nélkül; külön `fsync` és aktiváló könyvtárátnevezés sincs a hangtelepítőben.
4. A böngészős előnézet korábbi szinkron HTTP-küldése a lassú vagy szüneteltetett
   lejátszás alatt feltarthatta az ikonok HTTP-kiszolgálását. A hangminta most
   korlátozott számú aszinkron feladatban válaszol, SD-zár nélkül küld.
5. Egy korábbi GitHub Action hibáját a ZIP-diagnosztika fájlnévmásolásának
   Release/`-Werror` figyelmeztetése okozta. Ez külön fordítási hiba volt;
   nem bizonyította a készülékes SD-hiba okát.

Az alap `app_storage` modul, a csatolás és az SD-konfiguráció megegyezett a
főággal. Több változtatást együtt alkalmaztunk, ezért az egyedi DMA-puffer
vagy a korábbi külön szinkronizálás önálló szerepe nem bizonyított.
A privát csomag hoston kétszer, mind a 18 WAV bájtpontos egyezésével települt;
ez nem utal a csomag egészének hibás WAV-formátumára. A készülékes javulást
a felhasználó visszajelzése igazolja, nem önmagában a hostteszt.

## Megtartott és eltávolított diagnosztika

Eltávolítva: normál kamera HTTP-/bájtszámláló naplók, JPEG-előkészítés és
háttérfelszabadítás előtti/utáni memóriamérések, sikeres RTP-egyeztetés,
AEC-keretméret, RTP-forrás és normál hívásvégi összesítők.

Megmarad: kapcsolódási folyamatjelzés, konkrét SD-/WAV-/feltöltési hiba,
SIP-elutasítás, hanghardverhiba, RTP-időtúllépési számlálók és váratlan
hang-újraindulás fázisa. A forrásport-ellenőrzés, számlálók és újraindulási
védelmek működési részek, nem törölhető debugmaradványok. Jelszó és PCM-hang
nem kerül naplóba.

## Ellenőrzések és összevonás

A működési javítás `60463ba` forrásához tartozó
[teljes CI #58](https://github.com/ZoleeBB/ZoleeEnergyFlowDisplay/actions/runs/40174194020)
sikeres: teljes C/JS/protokoll/kiadási regresszió, Chromium desktop/mobil,
Windows Master/Slave és Linux Slave szimulátor, Master LVGL/ASan 32 bit és
RGB565, témák/ikonok, ESP-IDF 5.5.4 Master/Slave firmware, méret-/OTA-ellenőrzés
és USB-csomagolás. Ez a naplótisztítás előtti bizonyíték; a legutolsó commit
ellenőrzését nem helyettesíti.

Összevonás előtt:

1. A legfrissebb PR-commit teljes CI-je legyen zöld, a firmware-builddel együtt.
2. A Master és a 4B megfelelő binárisát kell használni; a két firmware nem
   felcserélhető. A végső tisztítás után érdemes egy feltöltés/témaváltás/
   csengetés/beszélgetés/kapunyitás próbát végezni.
3. A [fizikai átvételi mátrix](slave-intercom.md#ellenőrzött-állapot-és-fizikai-átvétel)
   más kameramodellre, több egyidejű Slave-re és kapcsolatvesztésre vonatkozó
   pontjai csak külön próbával tekinthetők igazoltnak. Az általános felhasználói
   visszajelzésből nem következik az összes variáció tesztelése.
4. Mainre **összevonás** indít automatikus, normál patch-kiadást. Puszta
   lezárás vagy jóváhagyás nem készít release-t. A #20 egyszeri v3.1.0 szabálya
   a #23 PR-re nem vonatkozik. [Kiadási folyamat](releases.md).

Részletek: [Kaputelefon](intercom.md), [Slave és hangcsomag](slave-intercom.md),
[SD-kártya](sd-card.md), [akkumulátor-statisztika](battery-diagnostics.md).
