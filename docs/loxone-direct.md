# Közvetlen Loxone adatforrás (v2.0.0)

Az elsődleges cél a helyi Gen2/v17 Miniserver. Régebbi generáció működése külön próbát igényel.

A `config.json` egyetlen új értéke: `"DataReceptMode": "loxone"`.
A Wi-Fi/NTP és a többi általános beállítás ugyanott marad. Módváltáskor a webes
beállításmentő újraindítja az eszközt. Hiányzó paramétereknél fekete,
Loxone-logós képernyő mutatja a paraméterezéshez szükséges helyi webcímet.

A kezdőlap és a Beállítások oldal Loxone módban elérhetővé teszi a
`/loxone` oldalt. Előbb Miniserver IP/gépnév, port, felhasználó és jelszó,
majd „Kapcsolat tesztelése és adatlista lekérése”. Ez aszinkron művelet:
a webfelület a háttérfeladat állapotát mutatja, nem blokkolja a HTTP kiszolgálót.
A lista csak a vizualizációban elérhető, a felhasználónak engedélyezett elemeket
kínálja fel. Javasolt külön kategória és felhasználó, virtuális állapotokkal.

**Energiaáramlás tiltás:** aktív állapotban csak a NightMode látható és kötelező;
a többi energia-párosítás megmarad. A Miniserver és az egyes funkciószakaszok
külön menthetők. [Működés és logós készenlét](energy-flow-disable.md).

Normál módban a kötelező mezők: PowerGrid, PowerBat, PowerPV, PowerFull, SocBat, Status,
NightMode és BatteryTemperature. Az öt időjárási mező kihagyható (null); az új relatív páratartalom
szorzóval/eltolással, egész százalékként használható: [beállítás és Slave-frissítés](relative-humidity.md).
A nappálya opcionális, négy külön numerikus párosítással: `SunAzimuth` (fok,
0 ≤ érték < 360), `SunAltitude` (fok, −90…90), `Sunrise` és `Sunset`
(egész perc éjféltől, 0…1439, napkelte < napnyugta). Például 07:00 = 420.
Mind a négy párosítást meg kell adni, vagy mind elhagyható; a nappálya az összes
kezdőérték beérkezése után jelenik meg. A nappályánál nincs szorzó és eltolás:
az értékeket a kijelző átalakítás nélkül veszi át.
A régi Loxone `SunPosition` szöveges párosítás helyett ezeket újra párosítani kell;
a többi mentett párosítás megmarad. Az MQTT/HTTP bemenet `SunPosition` szöveges
formátuma változatlan.
Virtuális analóg állapotnál `value`, digitálisnál `active`.
A lista két legördülővel, helyiség és funkció (Loxone-kategória) szerint szűrhető,
a két feltétel egyszerre érvényes. A lista neve elöl áll: `Név (Helyiség / Funkció) · állapot`.
A korábbi párosítás szűrés közben is megmarad; a mentett kulcs az állapot UUID-ja.
Számok átalakítása: `érték * scale + offset`. W → kW: 0.001; előjelváltás: −1;
digitális invertálás: scale=−1, offset=1. Az időjárási mezőknél (`WindSpeed`,
`HasSunRadiation`, `HasPrecipitation`), a nappályánál, a `Presence` és a
`MailArrived` jelnél a webes felület nem kínál szorzót és eltolást; ezekre
mindig `scale=1`, `offset=0` mentődik. A Status a kijelző 1–5 kódját követi.
Az **5 = Hálózat újra elérhető** a hálózati feszültség visszatérését jelzi,
érvényes üzemállapot, önmagában nem indít újracsatlakozást. Közvetlen numerikus
státuszforráshoz `scale=1`, `offset=0` használható. A teljes státusztáblázat és
a Master/Slave firmware-frissítési követelmények: [Status](mqtt.md#status).
A hálózati és akku-előjeleket a meglévő [adatprotokoll](mqtt.md) szerint kell beállítani.

## Külön SD-konfiguráció és eszközkulcs

A Miniserver adatai és párosításai kizárólag `loxone.json`-ba kerülnek.
A jelszó AES-256-GCM titkosítást kap, minden mentéskor új 96 bites nonce-szal és
128 bites hitelesítő címkével. A host, port és felhasználó is hitelesített adat.
A véletlen 256 bites kulcs a készülék NVS-ében él, nem az SD-kártyán.
Ez az SD önálló kiolvasása ellen véd; nem helyettesít flash encryption/secure boot
védelmet a készülék fizikai feltörésével szemben. NVS-törlés vagy másik kijelző
esetén a párosítások betöltődnek, a jelszót újra meg kell adni.
A webes API sem jelszót, sem titkosítványt, sem tokent nem ad vissza.
Mentés: tmp + flush/fsync + korábbi fájl biztonsági másolata + átnevezés;
megszakadt átnevezés után bootkor a biztonsági másolat helyreállítható.
Sikertelen SD-mentéskor a korábbi konfiguráció marad aktív.

## Kapcsolat és hibák

Helyi WS: titkosított RSA/AES Loxone-hitelesítés és JWT, de az állapotesemények
nem kapnak teljes adatfolyam-titkosítást. WSS (Gen2) esetén megbízható CA PEM-et
kell megadni, és a hostnak egyeznie kell a tanúsítvánnyal; nincs automatikus
ellenőrzés-kikapcsolás. A webes beállító ugyanazt az opcionális Basic Auth védelmet
használja, mint a meglévő eszközweb; helyi HTTP, nem HTTPS.

Az adatlista először WebSocketen érkezik. Ha a fájlkérés 404-es választ ad,
a kliens ugyanarról a Miniserverről HTTP(S)-en kéri le a `data/LoxAPP3.json`
fájlt. Ehhez friss HTTP hitelesítési kulcsból HMAC-et készít a meglévő JWT-vel;
a jelszó és a nyers token nem kerül a kérésbe. A HTTP(S) a kiválasztott WS/WSS
módot és a CA-ellenőrzést követi. Átirányítást nem követ, 401/403 esetén nem
próbál más jogosultságot. A fájl méretkorlátja ezen az úton is 512 KiB.

A kliens 15 másodpercenként Loxone keepalive parancsot küld; 45 másodpercnyi
bejövő üzenethiányt vagy azonnali socket/Wi-Fi hibát kapcsolatvesztésnek vesz.
Az újrapróbálkozás **változatlanul azonnal elindul**: 2/4/8/16/30 s,
legfeljebb 30 s. A változatlan Loxone-érték is érvényes: az aktuális
állapottárból 5 másodpercenként készül validált energiaminta.

**Új, 10 másodperces kijelző-türelmi idő:** ha a fő energiaáramlási téma
vagy a Loxone Otthon-oldal már érvényes adatokkal látható volt, a rövid
kapcsolat-/mintavesztés során az **utolsó kirajzolt kép változatlanul
megmarad**. Nincs „Újracsatlakozás” felirat, animáció, új mérési adat,
SD-s témabetöltés vagy teljes képernyős Loxone-hibakép. Az Otthon
vezérlőgombjai és a lapozás ebben az ablakban inaktívak; a kaputelefon
és a firmware-frissítés elsőbbsége megmarad. Ha a kapcsolat és a
**validált energiaminta** 10 másodpercen belül helyreáll, a kijelző
közvetlenül folytatja a normál működést. Ha nem, a korábbi
„Megszakadt a Loxone adatkapcsolat” képernyő jelenik meg.
Kezdeti indításkor, érvényes minta nélkül nincs befagyasztott téma:
a kapcsolódási képernyő változatlanul megjelenik. A türelmi idő nem
indul újra pusztán a WebSocket átmeneti kapcsolódásától.

A türelmi idő **csak a megjelenítésre vonatkozik**. A Loxone kliens
a megszakadást azonnal kezeli, az energiamintát érvényteleníti és
megszakítja a statisztikai folytonosságot; a kimaradt időszakra nem
számolunk kitalált energiát. Az érvényes Loxone kapcsolat sem
bizonyítja az inverter upstream adatainak frissességét.
Az új verzió OTA-érvényesítéséhez Loxone módban az elérhető webfelület
elegendő, nem kell 3 percen belül elvégezni az első párosítást.

## Erőforráskorlátok és ellenőrzés

Egy WebSocket, 8 elemű üzenetsor, legfeljebb 512 KiB struktúra/üzenet és 512
listázott állapot. A nagyobb adatlista hibával elutasított, nem részlegesen elfogadott.
A nem használt állapotok nem kerülnek tartós állapottárba. A kezdeti teljes JSON
feldolgozásakor átmeneti többletmemória szükséges. A JWT RAM-ban él. Megújítása a Miniserver `validUntil` időpontjából számított hátralévő idő felénél, legfeljebb óránként történik. Ismeretlen lejárat vagy még hiányzó pontos rendszeridő esetén az órás ütemezés érvényes. A sikeres megújítás nem bontja a WebSocketet és nem törli az állapottárat. Ha elutasítják, ugyanazon a kapcsolaton új tokent kérünk; csak kapcsolat- vagy hitelesítési hiba indít újracsatlakozást. Újraindításkor új hitelesítés történik.

A platformfüggetlen C-tesztek a valódi UUID/formátumkezelést, szorzókat,
hiányzó kezdőértékeket, null időjárást, numerikus/szöveges eseménytáblákat és hibás
csomagokat ellenőrzik. Tényleges Miniserver és fizikai kijelző nélküli teszt nem
igazolja a gyártói firmware-kompatibilitást vagy a panel FPS/memóriatartalékát.
Kiadás előtt: valódi Gen2/v17 kapcsolódás, lejárat/megújítás, jogosultságváltozás,
router/Miniserver újraindítás, SD-hiba, kártyaköltöztetés, 100 témaváltás és
legalább 24 órás járatás szükséges a belső RAM/DMA minimumának megfigyelésével.

Forrás: [Communicating with the Miniserver v17](https://www.loxone.com/enen/kb/api/),
[Structure File](https://www.loxone.com/enen/kb/api/),
[LxCommunicator.NET](https://github.com/Loxone/LxCommunicator.NET).

### Elvégzett ellenőrzések (2026-10-17)

- Teljes ESP-IDF 5.5.4 / ESP32-P4 fordítás: sikeres. Alkalmazás: 0x288090 bájt,
  a 8 MiB partícióban 68% szabad hely. Ez flash-kapacitás, nem RAM/FPS mérés.
- Teljes korábbi regressziós tesztsor és az új C-adatfeldolgozási teszt: sikeres.
- MbedTLS 3.6.5-tel valódi RSA/AES/HMAC kézfogás, tokenmegújítás,
  WS/driver-fragmentáció és túlcsordulás tesztje: sikeres.
- Valódi AES-GCM és fájlrendszeres eszközkulcs-/rongálás-/mentéshiba-tesztek: sikeresek.
- Valódi LVGL render mind a 18 témaváltozattal, külön Loxone beállító,
  kapcsolódási és kapcsolathiba-képernyő, automatikus visszatérés: sikeres.
- Valódi Miniserver és fizikai panel: a 2026-10-17-i ellenőrzés idején még nem volt visszaigazolva. A világításvezérlés későbbi, 2026-11-13-i felhasználói próbája az alábbi világítási fejezetben szerepel; ez nem helyettesíti az összes kapcsolat- és terhelési eset mérését.


## Jelenlét és postaláda a Master kijelzőn

Az **Adatpárosítás** két opcionális kijelzővezérlő sora:

| Mező | Érték | Hatás |
| --- | --- | --- |
| **Jelenlét a Master előtt** (`Presence`) | 0 / 1 (a pozitív érték aktív) | A Loxone oldalon külön beállítható, hogy nappal, éjszaka vagy mindkét napszakban sötétítsen. Aktív jelenlétnél világít; a jelenlét megszűnése után a beállított késleltetéssel teljesen elsötétül. Érintés vagy új jelenlét az adott napszak fényerejére ébreszt. |
| **Posta érkezett** (`MailArrived`) | 0 / 1 | 1 / true értéknél postaláda-ikon jelenik meg minden téma fejlécében. |

- Mindkettő opcionális. Párosítás nélkül a kijelző pontosan úgy működik, mint
  korábban.
- A jeleket ugyanaz a Loxone-kapcsolat hozza, azonnal (nem az 5 másodperces
  energiaminta-ciklusban). Kapcsolatvesztéskor ismeretlenné válnak: a kijelző
  ilyenkor nem sötétedik el, és a postaláda-ikon eltűnik.
- Hibaoldal (Wi-Fi, adatkimaradás, Loxone-hiba), kaputelefon-hívás és
  ismeretlen jelenlét mellett a kijelző mindig látható marad.
- Az ébresztő első érintés nem nyom meg közben gombot és nem lapoz.
- Energiaáramlás tiltás módban a két mező párosítható; a postaláda-jelzés ott a
  témák helyett nem jelenik meg, a jelenlétvezérlés ebben a módban nem aktív (a
  logós készenlét saját, 10%-os fényerejét használja).

A két jelnél nincs szorzó és eltolás: a 0/1 (illetve false/true) értéket a
kijelző közvetlenül használja. Ha a Loxone-oldali logika fordított, a
Miniserveren invertáld az állapotot.

### Mester jelenlét szerinti sötétítése

Az Adatpárosítás után külön szakaszban állítható a Master három paramétere:

| Beállítás | Jelentés |
| --- | --- |
| **Sötétítés nappal, ha nincs jelenlét** | A `NightMode = 0` állapotban is engedélyezi a jelenlét szerinti teljes sötétítést. |
| **Sötétítés éjszaka, ha nincs jelenlét** | A `NightMode = 1` állapotban engedélyezi a jelenlét szerinti teljes sötétítést. |
| **Sötétítési késleltetés (másodperc)** | 1–86400 közötti egész érték. A jelenlét megszűnése és az utolsó érintés után is ezt az időt használja. |

A pipák egymástól függetlenek: mindkettő bekapcsolható vagy kikapcsolható.
Amelyik napszak nincs bejelölve, abban a kijelző a beállított nappali vagy
éjszakai fényerőn marad. A napszakot a Loxone `NightMode` állapota adja;
a sötétítés nem módosítja a fényerő százalékos beállításait.

Folyamatos aktív jelenlét mellett nincs elsötétülés. Az ismételten érkező 0
érték nem indítja újra a visszaszámlálást; az új jelenlét megszakítja azt.
Minden érintés újraindítja az ébrenléti időt, és érintés után legalább
**15 másodpercig** világít a kijelző akkor is, ha a beállított késleltetés ennél
rövidebb. Például 30 másodperces beállításnál jelenlétvesztés után 30
másodperccel sötétül el; használat közben az utolsó érintés után indul ez a
30 másodperc. A napszakváltás nem dobja el a még le nem járt ébrenléti időt.

A beállítás a Master `loxone.json` fájljának `presence` objektumába kerül.
A régi fájlok továbbra is betölthetők: alapból a nappali pipa kikapcsolt, az
éjszakai bekapcsolt, az időérték pedig a korábbi
`config.json` → `display.touch_wake_seconds` értékéből indul (ennek alapértéke
60 másodperc). A következő mentéstől a Loxone oldalon megadott idő az irányadó;
az általános Beállítások oldalon nincs második, ettől független ébresztési idő.

A `GET /api/loxone` visszaadja a `presence` objektumot. A külön mentőgomb a
`POST /api/loxone/save` végponton csak ezt a szakaszt módosítja:

```json
{
  "section": "presence",
  "presence": {
    "blank_at_day": true,
    "blank_at_night": true,
    "timeout_seconds": 30
  }
}
```

A kéréshez a szokásos webes hitelesítés és az `X-Zolee-Loxone: 1` fejléc kell.
A `202 Accepted` a kérés átvételét jelzi; a felület a mentett állapot
visszaolvasása után igazolja a sikert. A Miniserver és az adatpárosítás külön
mentése megőrzi ezt a profilt.

## Slave-kijelzők jelenlétbeállítása

A Masteren kapcsold be az **Adat seed – master kijelző (BLE)** funkciót, majd a
Master diagnosztikáján megnyitott párosítási ablakban párosítsd a Slave-et a PIN-nel.
A Loxone beállító oldal **Slave-kijelzők** szakasza a már ismert, legfeljebb három
eszközt mutatja. Minden eszköznél saját név, Loxone jelenlétállapot, sötétítési
késleltetés (1–86400 másodperc), valamint külön nappali és éjszakai sötétítési
pipa adható meg. A helyiség/funkció szűrő
segít kiválasztani az állapotot. Üres forrásnál csak érintés ébreszt.

Új Slave-profilnál mindkét sötétítési pipa kikapcsolt. Meglévő profilnál a
korábbi éjszakai pipa és időérték megmarad, a nappali pipa kikapcsolt állapottal
egészül ki. Bekapcsolt napszakban a folyamatos jelenlét ébren tart, és a jel
megszűnése indítja a visszaszámlálást. Az érintés ugyanazt az időt indítja újra,
legalább 15 másodperces ébrenléttel. Az első teljes ébresztő érintés nem nyom
meg közben kártyagombot és nem lapoz.

Nappal ébren a Slave saját nappali fényereje, éjjel ébren a saját éjszakai
fényereje érvényes. A kártyán megadott fényerő felülírja az öröklést; az
üresen hagyott nappali vagy éjszakai mező a Master megfelelő fényerejét veszi
át. A két fényerő és a jelenlétprofil eszközönként külön állítható.
A nappali sötétítéshez **a Master és a Slave firmware-ét is frissíteni kell**.

Mentés után várd meg a **Beállítások tartósan elmentve** visszajelzést. Az átvett
kérés és a folyamatban lévő mentés még nem bizonyít tartós tárolást. Hiba vagy
kapcsolatvesztés esetén a helyi szerkesztés megmarad. Másik böngésző ugyanazon
eszközön végzett módosításánál a szerkesztést ellenőrizni, szükség esetén elvetni
kell; az újabb profil nem írható felül észrevétlenül.

A párosított profil törlése/bond-kezelése nem része ennek a webes szakasznak.
Részletes API, tárolási és BLE-szerződés: [Slave jelenlétprofil](slave-presence-protocol.md).


## Helyi energiaasszisztens (v3 fejlesztési ág)

A fogyasztónál engedélyezett **Indítás javasolható** és ismert átlagteljesítmény
mellett a Master címsávjában „Most érdemes…” jelzés jelenhet meg. Pontosan 100%
akku, legalább két perc elegendő, valódi napelemes többlet és ismert, álló
fogyasztó kell hozzá. Az akkutöltés elsőbbséget élvez. A jelzés nem kapcsolja a
gépet; az X becsukja és csendidőt indít. Az éjszakai magasabb alapfogyasztás
jelzéséhez legalább három, megfelelően mért korábbi éjszaka szükséges.

A jelzés külön helyet használ a Rendszer és a gyári témák címsávjában;
a fogyasztói/kártyaoldalak és a PIN-pad vezérlőit nem takarja. Csak Master/Loxone
módban működik. Részletek és a régi napfájlok kezelése:
[Helyi energiaasszisztens](energy-assistant.md).

## Kaputelefon

A Master MJPEG-élőképének, a csengetésállapotnak, az opcionális kapunyitónak és
a kézzel indítható kétirányú hangkapcsolatnak a beállítása: [Loxone kaputelefon](intercom.md).
Ugyanitt menthető külön a hangszóró hangereje és a mikrofon erősítése.
Az aktív beszélgetés csengetés nélkül is nyitva tartja a videót; elfogadott
kapunyitás után 5 másodperccel bont, és csengetés hiányában az ablak is bezárul.

## Elektromos autótöltés

A `/loxone` oldal **Fogyasztók** szakaszában az autótöltéshez tartozó mérőknél
válaszd az **Elektromos autótöltő** (`ev-charger`) ikont, és a szorzóval válts
kW-ra (W esetén `0.001`). Több ilyen fogyasztó érvényes teljesítménye összeadódik;
ugyanazt a mérést csak egyszer vedd fel. Az `electric-car` ikon és az
Otthon-kártyák nem számítanak bele.

A `11-electric-car` téma az élő teljesítményt, a mai töltési energiát és annak
becsült közvetlen napelemes arányát mutatja. Töltő nélkül is választható,
rejtett autós adatokkal; az Energiaáramlás tiltás ezt a témát is letiltja.
[Telepítés, adathiány és számítások](electric-car.md).


## Világításvezérlés és látható Otthon-oldal

A Világításvezérlő kártyához csak a `LightControllerV2` szülőblokkot kell
kiválasztani. A külön körök, a színkezelés és a zárolások automatikusan a
struktúrából érkeznek. [Használat és memóriahatárok](lighting-controller.md).

A feldolgozott kártyaértékek és világítási modellek csak a ténylegesen látható
Otthon-oldalhoz frissülnek. A konfigurált kártyákhoz szükséges utolsó nyers
numerikus és szöveges állapotot viszont a kapcsolat kezdetétől, rejtett nézetnél
is megőrizzük. Az oldal és a részletes blokk ebből épül fel: megnyitásuk nem
küld ismételt `enablebinstatusupdate` parancsot és nem bontja a WebSocketet.
A kezdeti, üres `jLocked` ismert feloldott állapot; a még nem fogadott,
hibás vagy valóban aktív zárolás továbbra sem enged vezérlést.

A szűrt nyers tár legfeljebb 512 külön UUID-t tartalmaz; a szövegek közös
kerete 64 KiB, állapotonként legfeljebb 32 KiB. A tartós tárolás PSRAM-ból
foglalódik, belső/DMA-heapre visszaesés nélkül. Rejtett oldalon nincs részletes
világítási modell vagy UI-frissítés. Ez helyi feldolgozási/tárolási szűrés,
nem szerveroldali UUID-előfizetés és nem a teljes Miniserver állapotmásolata.
Az energia-, fogyasztó-, jelenlét-, postaláda-, kaputelefon- és Slave-adatok
szükséges háttérkövetése változatlan.

Kapcsolatvesztéskor a világítási ablak a türelmi idő előtt bezáródik,
a nyers tár érvénytelenedik, a függő világítási parancsok nem játszódnak vissza.
A 2026-11-13-i felhasználói visszajelzés a javítás utáni működést a saját
kijelzőn és Miniserveren megerősítette. A korábbi kezdőállapot- és helyi
vezérlési hibát ezzel visszaellenőrzöttként tartjuk nyilván; ez nem RAM-,
DMA-, FPS- vagy tartósterhelési mérés.
[Statikus előnézetek és részletes ellenőrzési jegyzet](lighting-controller.md).
