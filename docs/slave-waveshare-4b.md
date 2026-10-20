# Waveshare 4B – 720×720-as Slave firmware

A fő Rendszer téma Energiaáramlás kártyája a Waveshare
ESP32-P4-WIFI6-Touch-LCD-4B (SKU 31416) eszközön, falon vagy asztali tartón,
SD nélkül, kizárólag Slave szerepben.

A [célprogram leírása](../firmware/slave-waveshare-4b/README.md) tartalmazza a
hardveradatokat, buildet, telepítést, szimulátort, közös modulokat, memóriakezelést
és hardveres ellenőrzőpontokat.

A [rétegleírás](../firmware/slave-waveshare-4b/design/README.md) rögzíti az
elfogadott tájat, az eltérő ablakfényes házakat és a natív energiakijelzést.
A képekre nem kerül mérési adat vagy háttérmaszk.

A fő kijelző külön cél marad. A közös BLE, PIN/profil, időszinkron, Wi-Fi és OTA
modulok biztosítják a protokollazonosságot. Az új panel saját 4B BSP-t és UI-t kap.
A szimulátor ugyanezt a UI-t fordítja.

## v3 ébresztési bekötés – fejlesztési állapot

A 4B és a fő kijelző Slave módja már a közös fényerő-állapotgépet használja.
Csak friss Master-minta változtathat nappalt/éjszakát; megszakadt kapcsolat vagy
lejárt minta az utolsó ismert napszakot megőrzi. Induláskor, PIN-bevitelkor és
használható érintőkapu hiányában a kijelző látható marad a saját fényerőértékein.

A nyers LVGL-érintőolvasás elé kötött közös kapu az első, sötétből ébresztő
érintés lenyomását, mozgását és felengedését is elnyeli. A következő érintés
már kattinthat. A BSP olvasója, koordinátái és adatmutatói megmaradnak.
A valódi LVGL-teszt a 720×720 és az 1280×800 szimulátorban is fut.

Az automatikus éjszakai elsötétítés **még kikapcsolva marad**: az eszközönkénti
stabil identitás, a Master webes jelenlétprofilja, a hitelesített BLE-átvitel és
az NVS-mentés a következő munkaszakasz. Régi Masterrel továbbra is a saját
folyamatos nappali/éjszakai fényerő érvényes; az érintéskapu önmagában nem kész
jelenlétfunkció. Fizikai paneles próba még szükséges.
