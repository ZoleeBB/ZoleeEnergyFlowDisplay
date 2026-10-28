# Waveshare 4B – 720×720-as Slave firmware

Az [Energiaáramlás tiltás](energy-flow-disable.md) külön szabályai: Loxone Otthon-alapnézet, megőrzött témák és Slave logós képernyő.

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

Az eszközönkénti stabil identitás, a Master NVS-profilja, a védett `1006` BLE-
olvasás, a Loxone WS-jelenlétút és a két panel közös profiladaptere be van kötve.
A Slave másodpercenként olvassa a 20 bájtos opcionális profilt. Rövid impulzus
számlálóval, tartós aktív jel szinttel jut át; késői/replay/lejárt válasz nem ébreszt.
SMP titkosítás/MITM/bond/16 bájtos kulcs kötelező.

Az új profilban a sötétítés **alapból kikapcsolt**. A Master Loxone webes
beállítójában a már párosított kijelző neve, jelenlétforrása, timeoutja és az
éjszakai sötétítés kapcsolója szerkeszthető. A felület külön jelzi az átvett,
függő, hibás és tartósan mentett konfigurációt; a helyi szerkesztés frissítéskor
megmarad. Régi Masterrel továbbra is a saját folyamatos nappali/éjszakai fényerő érvényes. A fogadott profil disconnect
alatt memóriában marad; saját fényerőinek korábbi NVS-beállítása nem változik.
Részletes szerződés: [Slave jelenlétprofil](slave-presence-protocol.md).
Fizikai paneles próba még szükséges.

## Mai összesítő

Vízszintes húzással a 4B két nézet között vált: energiaáramlás és **Mai
összesítő**. Az összesítő a mastertől a védett `1005` karakterisztikán kapott
napi adatokat mutatja (termelés, fogyasztás, vételezés, visszatáplálás, akku
töltés/leadás, önellátás, saját felhasználás, költség, bevétel, megtakarítás).
Első szinkronig `—`, kapcsolatvesztéskor az utolsó ismert mai kép látszik
elavultságjelzéssel. A napi statisztika-pillanatképet a firmware PSRAM-ba, csak
látható nézetnél és legfeljebb másodpercenként kéri le; üresjáratban a 33 ms-os
kör nem érvénytelenít. Kezelés: [gesztusok](gestures.md#waveshare-4b-slave-720720).

## Kaputelefon

A Masteren kezelt kamera, hang, kapunyitás és csengőhang a 4B-n is elérhető; a hívásfoglalás központi. [Részletes működés és hardveres átvétel](slave-intercom.md).
