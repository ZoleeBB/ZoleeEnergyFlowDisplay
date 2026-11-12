# Loxone világításvezérlő Otthon-kártyák

## Fejlesztési állapot

Fejlesztés alatt a `feature/loxone-lighting-cards` ágon. Ez a dokumentum az elfogadott követelményeket rögzíti; önmagában nem jelenti a firmware-funkció elkészültét. A megvalósítás és az ellenőrzések eredményeit a fejlesztés végén kell aktualizálni.

## Elfogadott működés

A webes kártyaszerkesztő új Világításvezérlő típusa automatikusan felismeri a `LightControllerV2` blokkokat. Csak a blokkot kell kiválasztani: a külön vezérelhető világítási köröket a struktúrából, a Loxone sorrendjét követve tölti be. Nincs egyedi körpárosítás, szűrés vagy sorrendszerkesztő. A következő sikeres struktúrafrissítés átveszi a körök változását.

A kis kártya megtartja a jelenlegi Otthon-kártya méretét, színvilágát és fejlécét. Alul `− / Ki / +` gombok, az ikon megérintése részletes ablakot nyit. A parancsok a blokk action címére mennek: `minus`, `changeTo/0`, `plus`. A kép nevét nem a helyi gombnyomásokból, hanem a visszajelzett állapotból állapítjuk meg.

Állapotfelirat:

- ténylegesen kikapcsolva: **Kikapcsolva**;
- egy aktív, azonosítható kép: annak neve;
- több aktív kép: **N aktív világítási kép**;
- bekapcsolt kör, azonosítható kép nélkül: **Egyedi beállítás**;
- még ismeretlen állapotot nem szabad kikapcsoltként feltüntetni.

## Részletes ablak

Középre igazított, körülbelül 900×680 képpontos ablak, elsötétített háttérrel. Rögzített fejléc és X, függőlegesen görgethető körlista. Minden kör neve egy sorban, helyhiányban három ponttal rövidítve jelenik meg. A teljes név megmarad az adatmodellben. Világítási képlista nincs.

- Kapcsolt kör: csak be/ki kapcsoló.
- Dimmer: csak 0–100% csúszka.
- RGB/RGBW: kompakt sor, lenyitható színtárcsa, külön Színes és Fehérfény mód, közös fényerő. Külön be/ki kapcsoló nincs. A 0% a kikapcsolt érték.
- Egyszerre csak egy színválasztó nyitható. Másik megnyitása az előzőt összecsukja, világítási parancs nélkül.
- A színes mód HSV-értéket, a fehér mód fényerőt és Kelvin-értéket ad át. A fizikai RGBW/WW-keverést a Loxone végzi. A képességeket és a TWMin/TWMax tartományt tiszteletben kell tartani.
- Az ablak X-szel vagy a háttér érintésével záródik, automatikus időkorlát nélkül. A háttérérintés nem aktiválhat mögöttes kártyát.
- A kaputelefon és a meglévő hiba-/frissítési képernyők elsőbbsége megmarad.

## Vezérlés, szinkronizálás és életciklus

Az értékek érintés közben folyamatosan változnak. A hálózati küldés körönként legfeljebb 10 parancs/másodperc; csak a legfrissebb függő értéket tároljuk. Elengedéskor a végérték nem veszhet el, és nem sérülhet a küldési időköz. A kapcsolók azonnali parancsot küldenek.

A nyitott ablak követi a külső kapcsolást, fényerőt, színt és színüzemmódot. Az aktívan húzott vezérlőt a késleltetett visszajelzés nem rángathatja vissza, érintés alatt üzemmódváltás sem történhet. Elengedés után az állapotot a Miniserverrel egyeztetjük. A többi kör közben tovább frissül.

Kapcsolatvesztéskor az ablak azonnal bezáródik; a függő parancsokat törölni kell. A meglévő Otthon-oldali 10 másodperces türelmi idő és hibakezelés marad. Újracsatlakozáskor nincs parancs-visszajátszás vagy automatikus ablaknyitás.

## Memória és láthatóság

A csak Otthon-kártyákhoz tartozó élő állapotokat kizárólag az Otthon megnyitásakor, az aktuális oldalon látható kártyákhoz kell frissíteni. A részletes világításállapot csak a megnyitott blokkhoz kell. Elrejtett oldalak és bezárt színválasztók nem tarthatnak életben nagy UI-/állapotpuffert vagy külön feladatot.

A láthatósági optimalizálás nem szüntetheti meg a továbbra is szükséges energia-, időjárás-, jelenlét-, postaláda-, kaputelefon-, Slave- és energiaasszisztens-adatok követését. Azonos UUID több felhasználóját is figyelembe kell venni. Oldalnyitáskor az újonnan követett állapotokat frissíteni kell; egy korábban kihagyott, azóta változatlan értékre nem szabad örökké várni. A Miniserver hálózati előfizetési lehetősége és a helyi feldolgozás szűrése nem ugyanaz.

Nagy struktúrák ne kerüljenek a verembe. A nagy, tartósan nem szükséges adatok igény szerinti, korlátos PSRAM-tárolót kapjanak. Változatlan LVGL-értékeket ne írjunk újra. Nincs külön WebSocket vagy világítási háttérfeladat.

## Ellenőrzési terv

Parser/command tesztek: action címek és subcontrol utótagok, Switch/Dimmer/ColorPickerV2, TW-tartomány, ismeretlen típus és hibás/sérült JSON, jogosultság, üres és nagy blokkok, képállapotok, UTF-8 nevek, körsorrend és dinamikus nevek.

Runtime tesztek: csak látható oldal, oldalváltási kezdeti állapot, közös UUID, 100 ms-os korlátozás, végérték, külső frissítés aktív érintés alatt/után, bontás és visszacsatlakozás, konfigurációcsere, ablaknyitás/-zárás, nincs elavult parancs.

UI/browser: webes blokkfelismerés és mentés, kompakt kártya, görgetés, ellipszis, egyetlen nyitott RGBW-panel, színtárcsa és fehér mód, háttérérintés, kaputelefon elsőbbség, változatlan frissítéskor nincs felesleges invalidálás.

Teljes regresszió, érintett szimulátor és mindkét firmware cél buildje; CI-állapot az utolsó commiton. Fizikai panel és valódi Miniserver nélkül a RAM/DMA/FPS-tartalék, az RGBW-keverés és a firmware-kompatibilitás nem tekinthető hardveresen igazoltnak.

Forrás: Loxone Structure File v17, 2026-03-31, LightControllerV2 és ColorPickerV2; https://www.loxone.com/enen/kb/api/ .
