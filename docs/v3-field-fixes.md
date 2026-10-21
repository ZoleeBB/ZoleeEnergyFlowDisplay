# v3 – felhasználói próba utáni javítások

A fejlesztési terv F01–F11 javításai elkészültek. A felhasználó 2026-10-21-én
igazolta a vezérlések, új témák, háttér és villogásjavítás működését, és kérte
a PR előkészítését. Ezután új, visszatérés utáni navigációs/terhelési hibát jelentett (F12); az új javítás fizikai elfogadása még nyitott. Részletes lezárás: [v3 ellenőrzés](v3-completion-review.md).

## Változások

- A `loxone-home.zip` v3 tartalmazza a 400×250-es világos/sötét, valódi
  LVGL-előnézeteket. Ezek csak a webes kezelőhöz kellenek, aktív kijelzőképként
  nem töltődnek be. A négy 1280×800-as háttér és a 64 darab 96×96-os ikon
  változatlan. A régi v1/v2 csomag továbbra is támogatott.
- A sablonkezelő a közös csomag képét is megmutatja. Telepített csomagnál
  az Otthon-szerkesztőben a kiválasztott ikon mellett 64×64-es előnézet látható.
  Törlés/telepítés állapotát a következő háttérfrissítés átveszi, a helyi
  szerkesztést megőrzi. Hiányzó csomagnál legfelül piros figyelmeztetés jelenik meg.
  A védett ikonvégpont kizárólag a rögzített könyvtár biztonságos nevű kis PNG-it
  szolgálja ki; telepítés nélkül 404, hitelesítés nélkül nincs hozzáférés.
- A Loxone-worker a kivett vezérlési kérés korát friss idővel ellenőrzi.
  Korábban a fogadási várakozás alatt beérkező kattintás a ciklus régebbi
  időpontjához képest jövőbelinek számított, ezért küldés nélkül kieshetett.
  A kapcsoló sikerét továbbra is bináris állapot-visszajelzés igazolja;
  az impulzust nem ismételjük automatikusan. A jogosultsági és célobjektum-kapuk maradnak.
- A vezérlési objektum stabil azonosítója külön marad a `uuidAction`
  parancscímtől. Az eltérő cím a friss katalógusból oldódik fel, így a már
  mentett kártyákat nem kell újrapárosítani. Ha nincs `uuidAction`, a korábbi
  érvényes objektumkulcs használható; explicit hibás címnél nincs visszaesés.
  A szerkesztő az ilyen objektumot csak állapotforrásként kínálja fel.
- Otthon/fogyasztók bezárása után a régi objektumok felszabadítását látható
  betöltőkép követi, még az SD-olvasás előtt. Diagnosztika után a már RAM-ban
  lévő téma is visszaépülhet a betöltőkép után, nem ragad rajta.
- Foglalt SD/katalógus mellett a betöltőképernyő objektumai nem épülnek újra
  ciklusonként; a spinner folytatódik. A Loxone-erőforrás betöltése rövid
  ideig újrapróbálható, az oldal nem záródik be az első foglaltságra.
  Tartós hiba 15 próbálkozás után RAM-eseményt rögzít és visszatér a témára.
- Rendszer nézetben a házikon átlátszó képtartója átengedi az érintést a
  fogyasztói oldal megnyitását végző szülőnek. A kép közepe is kattintható.
- A Bluetooth-gomb felirata két tengelyen középre kerül; az ikon balra áll.
- A közös felirat: „Adatok és grafikai elemek betöltése...” / “Loading data and graphics...”.
- Sablontörléskor azonnali forgó kör, szöveges állapot és `aria-busy` jelzés;
  a lista a kérés végéig zárolt. Hiba után újra használható, siker után frissül.
- Az Energiakör előnézete most érvényes napadatú, tényleges idővonalas LVGL-frame.
  A webes ZIP-validátor az új `solar_timeline` elemet és minimum méretét is ismeri.

## Actions-csomagok

Négy normál kimenet marad: `zolee-firmware-<commit>`,
`zolee-templates-<commit>`, `zolee-simulator-master-windows-<commit>`, `zolee-simulator-slave-4b-windows-<commit>`.
Az artifactok és a letöltő kiadási workflow-k együtt kapták meg az egységes neveket.
Külön OTA-másolat, letölthető Linux-szimulátor, PDB és nagy tesztkép-artifact nincs.
A regressziós, böngészős és Linuxos/LVGL tesztek megmaradtak. Korábbi távoli
artifactokat nem törlünk. Részletes tartalom: [releases.md](releases.md).

## Ellenőrzés és eszközpróba

A célzott protokollteszt a worker várakozása alatt érkező impulzust és
kapcsolást reprodukálja; vizsgálja a küldést, a nyugtát, az állapotot,
az elutasítást és az ismétlés hiányát. A valódi HTTP/JS-tesztek ellenőrzik
a csomaghiányt, telepített ikonokat, hitelesítést, útvonalakat, törlési várakozást,
hibából visszaállást és a szerkesztés megőrzését. A C és a webes csomagvalidátor
az új előnézetes és a régi csomagot is elfogadja, a hibás változatokat elutasítja.
A katalógus–parancs teszt az eltérő/azonos/hiányzó `uuidAction` címet,
érvénytelen karakterláncot, üres/numerikus/null címet, állapot nélküli gombot
és nem UUID objektumkulcsot is vizsgál. A titkosított protokollteszt a
ténylegesen küldött BE/KI/impulzus parancs címét ellenőrzi.

Valódi LVGL-tesztek: beágyazott házkép érintése, két sikertelen betöltési
próba után helyreállás ugyanazzal a betöltőobjektummal, tartós hiba utáni
visszatérés, háttér megjelenése, 100 változatlan fogyasztói frissítés alatt
nulla újrarajzolási kérés, bezárási betöltőképek és Bluetooth-felirat középpontja.
A renderelt fogyasztói háttér és a középre igazított diagnosztikai gomb vizuálisan
is ellenőrzött. A teljes natív regresszió sikeres. Az új távoli CI és a
firmware-méretek a fejlesztési tervben kapnak végleges eredményt.

Az új firmware és az új `loxone-home.zip` telepítése után fizikailag ismételendő:
kapcsoló BE/KI valódi visszajelzéssel, egyetlen impulzus valódi hatása,
fogyasztói háttér és villogás (belépés/betöltés/nyugalmi nézet), mindhárom
bezárási út, Rendszer-házikonnal megnyitás, Bluetooth-felirat, webes
ikonválasztás csomaggal és nélküle, lassú sablontörlés. A panel/Miniserver
próbája nem történt meg ebben a környezetben; a szimuláció ezt nem igazolja.

## F11 – villanó panel és hiányzó fogyasztói háttér

A 2026-10-21-i új panelvideó alapján a hiba továbbra is fennállt: a betöltő-
és fogyasztói oldalon kékes felvillanás, hiányzó fényképes háttér és egyes
szürke ikonok. A végső javítás: teljes képkockás, hárompufferes panelátadás;
előre ellenőrzött Home-képek, natív RGB565 háttér és előállított szürke ikonok;
dekóderhiba esetén teljes visszavonás és korlátos újrapróbálás. Új SD-csomag
nem szükséges ehhez a firmware-javításhoz, a meglévő v3 csomag használható.

A háttér tényleges renderelt képpontjait, sérült képek kezelését és ismételt
nyitás/lapozás/bezárás esetén a pufferfelszabadítást automatizált próba
ellenőrzi. A panelvillanás megszűnése és a teljes képkockás mód FPS-e csak
fizikai visszaméréssel fogadható el. A felhasználó az `adfa395` firmware után
igazolta a villogás megszűnését és a háttér megjelenését: a funkcionális hiba
lezárva. Számszerű FPS-/memóriamérésről nem adott visszajelzést.

## F12 – Otthon után elvesző navigáció, betöltőkép és terhelés

A gesztus inputszintre került, az újraépülő objektumfa buborékolásától
függetlenül. A fix képernyők nem görgethetők, az inputhoz egyszer kerül a
callback. Az adapter saját LVGL-feladatának flush-visszajelzése megelőzi
az SD-munkát; a runtime a várakozás alatt elengedi a lockot. A desktop runtime
is követi az éles előkészítési sorrendet. A nehéz görbék 50 ms-os kerete megmarad; a normál frissítés végső
ütemezését az alábbi F13 írja le.

A célzott valódi LVGL-próba négy témán, nappal/éjjel, fogyasztói és kártyás oldalról X-szel
visszatérve ellenőrzi az alsó szélről felhúzott diagnosztikát, mindkét
energiatéma-irányt, egy gesztust blokkoló fedőelemet, a betöltési flush sorrendjét
és az egyszeri callbackkötést. Ez a korábbi objektumszintű navigációnál a
fedőelemen elakad. A teljes 100 ciklusos erőforrás-/ASan-próba megmarad.
Új grafikai csomag nem kell. A tényleges panel javulása és számszerű terhelése
külön eszközpróbát igényel; memóriaszivárgást mérés nélkül nem állítunk.

## F13 – minden témán akadó animáció és indulástól elvesző húzás

A felhasználó az F12 után Home-nyitás nélkül is akadozó animációt és szinte
működésképtelen húzást jelzett. A 33 ms-os normál animációt és a kész frame
utáni újabb 33 ms várakozást visszavontuk: a korábban elfogadott 15/16 ms-os
refresh/animáció tér vissza. Az animációból kikerült az inputcallbackek
ismételt keresése. A teljes képkockás villogásjavítás megmarad.

A gyári touch-adapter két IRQ között az utolsó koordinátát ismétli. Az LVGL
alap sebességküszöbe ennél törli a húzás összegét. A küszöb nulla, az 50 px-es
távolságfeltétel megmarad. A célzott próba a javítás előtt már a beépített
első balra húzásán elbukott; az új változat mind a tíz témán, Home megnyitása
nélkül vizsgál bal/jobb/felfelé húzást ismételt pontokkal. A 15 ms-os redraw,
a 90 ms-os lassú frame utáni folytatás és a Home-visszatérési próbák is részei.
A betöltőkép visszaigazolása és a pufferfelszabadítás nem változott.
Új SD-csomag nem szükséges. A fizikai elfogadás újra nyitott.
