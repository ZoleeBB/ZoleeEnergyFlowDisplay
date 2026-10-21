# v3 – felhasználói próba utáni javítások

A fejlesztési terv 19. checkpointja, F01–F10. Nincs PR vagy kiadás;
a fejlesztési ág feltöltése továbbra is engedélyezett.

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

Négy normál kimenet marad: `firmware-packages-<commit>`,
`template-packages-<commit>`, `windows-ui-simulator`, `windows-slave-4b-simulator`.
A közös firmware-artifact neve és a két release workflow hivatkozása változatlan.
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
