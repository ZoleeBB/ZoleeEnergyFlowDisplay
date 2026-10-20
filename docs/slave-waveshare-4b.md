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
