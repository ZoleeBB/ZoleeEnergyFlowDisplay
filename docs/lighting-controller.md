# Loxone világításvezérlő Otthon-kártyák

## Elérhetőség és beállítás

A funkció a Master 10.1 kijelző közvetlen Loxone üzemmódjának Otthon-oldalait bővíti. A `LightControllerV2` blokkokat kezeli; a régi `LightController` nem azonos protokoll. A Slave 4B nem kap külön világításvezérlést vagy új Bluetooth-adatfolyamot.

A `/loxone` weboldalon kérd le a Loxone adatlistáját, majd az Otthon-kártyák között válaszd a **Világításvezérlő** típust. Add meg a kártya nevét és ikonját, és válaszd ki a Loxone világításvezérlő blokkját. Külön visszajelző állapotot vagy világítási köröket nem kell párosítani. Az Otthon-oldalak a többi funkciótól függetlenül menthetők.

A körök neve, típusa, parancscíme, visszajelzése és fehérfény-tartománya a Loxone struktúrájából jön. A sorrend a struktúra `subControls` sorrendje; nincs saját rendezés vagy elrejtési lista. A gyártói `masterValue`/`masterColor` összesített segédvezérlők nem külön fizikai körök. Az ismeretlen típusú valódi körök is megjelennek, de vezérlésük tiltott. A körök hozzáadása/törlése a következő sikeres struktúralekéréskor érvényesül; a struktúrát nem kérjük le minden érintéskor. A visszajelzett `circuitNames` névváltozások nyitott ablakban is frissülnek.

## Kis kártya

A méret változatlan: 292×200 képpont, a meglévő 12 kártyás oldalelrendezésben. Az ikon nyitja a részletes kezelőt. Alul `− / Ki / +` gombok vannak; a kártya nem nő dupla szélességűre.

| Gomb | A blokk action címére küldött parancs |
| --- | --- |
| − | `minus` |
| Ki | `changeTo/0` |
| + | `plus` |

Ezek dokumentált vizualizációs parancsok: nem új virtuális bemeneteket vagy fizikai `M+`/`M-`/`Off` bekötést igényelnek. A visszajelzés nem a helyi gombnyomások számolásából készül.

| Visszajelzett állapot | Felirat |
| --- | --- |
| Minden ismert világítási kör ténylegesen kikapcsolva | Kikapcsolva |
| Egy azonosítható aktív világítási kép | A kép neve |
| Több aktív kép | N aktív világítási kép |
| Van bekapcsolt kör, azonosítható kép nélkül | Egyedi beállítás |
| Hiányzó/érvénytelen kezdőállapot | Nincs adat |

A hiányzó képnév önmagában nem jelent kikapcsolt állapotot. A hosszú feliratot a rendelkezésre álló helyhez igazítva rövidítjük.

## Részletes kezelő

Középre igazított 900×680 képpontos ablak, elsötétített háttérrel, rögzített címmel és X bezárógombbal. A körök egy oszlopban, függőlegesen görgethetők. A nevek egysorosak, helyhiányban három ponttal rövidítve. Világítási képlista nincs: a képek csak a kis kártya gombjaival léptethetők.

- **Kapcsolt kör:** csak be/ki kapcsoló.
- **Dimmer:** csak 0–100% fényerőcsúszka.
- **RGB/RGBW:** kompakt név/szín/fényerő sor, érintésre lenyitható részletekkel. Egyszerre egy kör lehet lenyitva. Nincs külön be/ki kapcsoló; a 0% jelenti a kikapcsolást.

A lenyitott kezelő külön **Színes** és **Fehérfény** módot, közös fényerőcsúszkát ad. Színes módban HSV-színtárcsa, fehér módban Kelvin-szabályozás működik. A `TunableWhite` körnél a színes mód tiltott; az `Rgb` és `Lumitech` körök képességeit a struktúra határozza meg. A `TWMin`/`TWMax` határokat betartjuk. A meleg/hideg fehér előnézet csak szemléltetés, nem színmérési referencia.

A parancs `hsv(H,S,V)` vagy `temp(V,K)`. A fizikai RGBW/WW-csatornák keverését **a Loxone** végzi; a kijelző nem számol saját négycsatornás keverést. Ennek konkrét eredményét az adott aktorral és Miniserverrel kell ellenőrizni.

Az ablak X-szel vagy a sötét háttér érintésével záródik. A bezáró érintés nem aktiválja az alatta lévő kártyát. Nincs automatikus bezárási idő. Másik RGBW-kör megnyitása a korábbit csak összecsukja, nem küld világítási parancsot. Éjszakai módváltás nem dobja el a nyitott kezelőt; az ablak stílusa bezárás után igazodik hozzá.

## Vezérlés és hibakezelés

A csúszkák és a tárcsa érintés közben vezérelnek. Körönként legalább 100 ms telik el két küldés között; a köztes értékek helyett csak a legfrissebb függő értéket tartjuk meg. Elengedéskor a végérték bekerül ugyanebbe a korlátos sorba, nem vész el és nem kerüli meg a küldési korlátot. A kapcsoló érintéskor azonnal sorba állítja a parancsot. A tényleges küldést a meglévő Loxone-worker végzi, nem az LVGL callback.

A nyitott kezelő folyamatosan követi a külső kapcsolást, fényerőt, színt és színüzemmódot. A húzott vezérlőt és módját érintés alatt nem módosítja a visszajelzés; a többi kör tovább frissül. A rövid ideig késő visszhang ellen a küldött érték legfeljebb két másodperces visszaigazolási ablaka véd. A parancs ACK-ja nem helyettesíti az állapot-visszajelzést.

Kapcsolatvesztéskor az ablak bezáródik, a függő parancsok, a nyers állapottár és a munkamenet-azonosítók érvénytelenednek. A meglévő Otthon-oldali tíz másodperces türelmi idő/hibaképernyő érvényesül. Újracsatlakozáskor nincs automatikus ablaknyitás vagy elavult parancs-visszajátszás. A már elküldött parancs természetesen nem vonható vissza. A kaputelefon, OTA és hibaképernyők elsőbbsége megmarad.

A mentés, oldalváltás és ablakbezárás is érvényteleníti a régi kezelő munkamenetét. A Loxone `isSecured`, read-only korlátozásai és `jLocked` állapotai nem kerülhetők meg. Ismeretlen képesség vagy hiányzó állapot esetén a rendszer nem találgat parancsot. A beérkezett, üres `jLocked` ismert feloldott állapot; a még nem kapott állapot nem ugyanaz.

## Memória és láthatóság

**Zárt Otthon-oldalnál nincs világítási UI-/blokkmodell.** Nyitva csak az aktuális oldal legfeljebb 12 kártyáját dolgozzuk fel. A fogyasztókhoz, energiához, időjáráshoz, jelenléthez, postaládához, kaputelefonhoz és Slave-funkciókhoz szükséges adatok ettől függetlenül tovább élnek, közös UUID esetén is.

**Protokoll miatti szükséges kivétel:** a konfigurált Otthon-kártyák utolsó nyers numerikus/szöveges állapotát a kapcsolat idejére megőrizzük, rejtett oldalnál is. A Miniserver az első `enablebinstatusupdate` után teljes állapotot, utána változásokat küld. Az ismételt engedélyezés nem használható garantált új snapshotként. Az összes adat eldobása ezért „Nincs adat” kijelzést és a hiányzó zárolásállapot miatt tiltott vezérlést okozott.

A szűrt nyers állapottár csak a felvett kártyákhoz tartozó UUID-ket tartalmazza, duplikáció nélkül. Nem a teljes Miniserver állapotmásolata. Rejtett oldalon nincs világítási JSON-értelmezés, színkeverés, körmodell vagy LVGL-frissítés: csak az utolsó érték cseréje. A háttérben nincs új lekérdezés vagy második WebSocket. Oldal- és ablaknyitáskor a legutóbbi fogadott értékek töltik fel a szükséges modellt, hálózati újraengedélyezés és kapcsolatbontás nélkül. A parancsok nem kerülnek ebbe a tárba.

A nyers tábla 16 bejegyzéstől, igény szerint nő, legfeljebb **512 UUID-ig**; a szövegek közös foglalási kerete **64 KiB**, egy állapot szöveghatára továbbra is 32 KiB. A tábla és a szövegek PSRAM-ból foglalódnak; nincs belső/DMA-heapre visszaesés, új task vagy verembővítés. Tárolási hibánál az adott régi érték érvénytelenedik, különösen nem maradhat régi feloldott zárolás. A még nem ismert vagy elvesztett érték nem válik kikapcsolt állapottá.

A látható kártyák a körök kompakt UUID/típus/ismert/be állapotát használják; nincs minden rejtett kártyához teljes körlista és színválasztó. Azonos blokk két látható kártyája közös kompakt állapotot használ. A host C-tesztben egy kompakt kör 48, egy teljes kör 376 bájt; ez ABI-mérés, nem az eszköz teljes RAM-igénye.

Csak egy megnyitott blokkhoz létezik teljes részletes állapottár. A görgethető lista legfeljebb **8 LVGL-sort** tart életben, függetlenül a körök számától. A 192×192 RGB565 színtárcsa **73 728 bájtos**, kizárólag szükség esetén foglalt PSRAM-puffer. Fehér mód, összecsukás, képernyőváltás és bezárás felszabadítja. Nincs SD-ről olvasott tárcsakép vagy állandó nagy veremtömb.

Korlátok: blokkonként legfeljebb 64 kör, 128 világítási kép. A nagyobb/ismeretlen blokk nem jelenhet meg megtévesztően részleges, vezérelhető állapotként. A neveket UTF-8 karakterhatáron, korlátos munkapufferekben kezeljük; az eredeti struktúra a katalógusban marad. A korábbi veremvédelmi ellenőrzés az új `lighting_cache_*` függvényeket is vizsgálja.

## Automatizált ellenőrzések

A `tests/test_lighting.py` a valódi parser/állapot/parancs modellt járatja AddressSanitizerrel: címek, segédvezérlők, sorrend, képállapotok, HSV/TW, jogosultság, zárolás, hibás és túlméretes adatok, közös/kompakt állapot, parancsösszevonás és látható oldal.

A `tests/test_loxone_protocol.py` a tényleges klienssel és titkosított teszt-Miniserverrel ellenőrzi a kapcsolat kezdetén, még zárt UI mellett érkező állapotok megőrzését. A teszt egy kapcsolaton csak egyszer küld teljes kezdőállapotot, benne a szülő és a dimmerkör üres `jLocked` állapotával. Az oldal-/ablaknyitás nem kérhet újraengedélyezést. Ellenőrzött esetek: három kártyagomb, dimmerparancsok, 100 ms-os korlátozás, rejtett oldalon változó érték újranyitáskor, valódi/hibás/hiányzó zárolás, memóriakorlát, közös UUID, kapcsolatbontás és konfigurációcsere. A fényerőadat önmagában nem oldhat fel ismeretlen zárolást.

A `tests/test_loxone_home_web.js` a valódi szerkesztő JavaScriptjével ellenőrzi a blokk automatikus kiválasztását, a külön állapotpárosítás nélküli mentést és a szűréskor megtartott kiválasztást.

A natív `tests/preview` cél `ZOLEE_LIGHTING_ONLY=1` módban a firmware tényleges LVGL-kódját futtatja 32 bites és RGB565 kijelzővel: kártyafeliratok/gombok, modal-geometria, kapcsoló/csúszka/tárcsa, érintés alatti szinkron, egyetlen picker, háttérérintés, 64 kör mellett nyolc sor, kapcsolatvesztés, ismételt megnyitás/bezárás és változatlan állapotban nulla felesleges invalidálás. Képei szintetikus Loxone-adatokkal készülnek, nem fizikai panelről.

A teljes regresszió, Windows szimulátorok és mindkét ESP-IDF firmware eredménye a PR utolsó commitjának CI-ellenőrzéseiben követhető. A 2026-11-13-i készülékes hibajelzés igazolta, hogy a régi kezdőállapot-kezelés nem volt megfelelő. **A javított verzió fizikai paneles/Miniserveres eredménye még nem igazolt.** Próba: telefonos beavatkozás nélküli első nyitás, −/Ki/+, dimmer/RGBW vezérlés, zárt nézet közbeni külső változás utáni újranyitás, zárolás és újracsatlakozás. A RAM/DMA/FPS-tartalékot, több nagy blokkot, kaputelefont és tartós járatást is mérni kell.

Forrás: [Loxone API](https://www.loxone.com/enen/kb/api/), Structure File v17 (2026-03-31), 16–17. oldal, LightControllerV2 és ColorPickerV2; Communicating with the Miniserver v17, 17. oldal, kezdeti állapottáblák és változásküldés.
