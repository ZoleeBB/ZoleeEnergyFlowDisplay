# microSD

Az opcionális microSD-kártyát FAT32-re formázd. Ha előre beállított kijelzőt szeretnél, a kártya gyökerébe másold a kiválasztott mintát `config.json` néven. A 10,1 hüvelykes kijelző induláskor először az érvényes SD-konfigurációt használja. [Konfigurációs minták és mezők](configuration.md)

A tíz választható nézet könyvtárai a `/templates/<azonosító>/` alatt legyenek. Minden csomag saját `template.json`-t és az abban hivatkozott PNG-ket, valamint `preview-light.png` és `preview-dark.png` képet tartalmaz. A régebbi, előnézet nélküli csomag továbbra is betölthető; a webes keret csak a frissítés után kap képet. A kiválasztás `selection.*` fájljait a készülék kezeli. Másoláskor a csomag JSON-ját és képeit együtt frissítsd; [telepítési útmutató](templates.md).

A készülék a csomagok mellé `<alap>-<szélesség>x<magasság>.raw` segédfájlokat
készít a háttérben: ezek a már dekódolt és a megjelenítési méretre hozott képek,
tőlük lesz gyorsabb a témaváltás. Együtt a tíz gyári témánál nagyjából 45–90 MiB
helyet foglalnak, a `.tmp`/`.bak` állományokhoz hasonlóan a készülék kezeli őket,
és a csomag törlésével eltűnnek. Kézzel nem kell másolni vagy pótolni őket; ha
letörlöd, a kijelző a PNG-kkel dolgozik tovább, és a háttérben újra elkészíti
őket. A háttérgenerálás kihagyja a lépést, ha a kártya hibásra van jelölve
vagy 64 MiB-nál kevesebb a szabad hely: betelt vagy haldokló kártyát nem
terhel tovább, és a statisztika írása marad az elsőbbség.
[Részletek](performance.md).

A statisztikát a firmware maga hozza létre és 35 helyi naptári napig őrzi. Egy nap 288 ötperces időablakot tartalmaz. A `.tmp` és `.bak` fájlok helyreállítási állományok: működés közben ne töröld őket. Induláskor a legújabb érvényes revízió kerül betöltésre. [Adatformátum és számítás](statistics.md).

Ha az SD hiányzik vagy a konfiguráció nem használható, a kijelző a belső beállítását vagy használható korábbi mentését tölti be. Első üzembe helyezéskor megadhatod a Wi-Fi-t az érintőképernyőn. Időszinkron után a kijelzett helyi címen választasz üzemmódot. A kártya nélküli 10,1 hüvelykes Master általános webes beállításai a belső NVS-tárba kerülnek. A fájlos sablonokhoz, csengőhangokhoz, a Loxone konfigurációs fájljaihoz és a tartós helyi statisztikai archívumhoz használj SD-t. A már üzemelő SD-s kijelző kártyahibáját a diagnosztika jelzi. Kártyacsere és kézi fájlmódosítás előtt kapcsold ki a készüléket. [Első indítás kártya nélkül](first-run-wifi.md)

## Slave SD-kártyával és anélkül

| Tulajdonság | SD-s Slave | SD nélküli Slave |
| --- | --- | --- |
| Konfiguráció | SD `config.json`, minimálisan `{"DataReceptMode":"slave"}` | Belső NVS, webes mentéssel, legfeljebb 3072 bájt |
| Témák | Rendszer és mind a tíz telepített SD-s téma; a Nap pálya érvényes nappályaadattal | Kizárólag a beépített Rendszer téma |
| Napi statisztika | Saját integrálás a fogadott élő mintákból, saját SD-re mentve | A master ellenőrzött pillanatképe RAM-ban |
| Korábbi adatok | Saját, legfeljebb 35 napos történet | Mai diagramok és a master tegnapi összehasonlítása; nincs helyi archívum |
| Újraindítás | Saját SD-előzmény visszatöltése | Új statisztikai kép kérése a mastertől |
| Wi-Fi, idő és automatikus OTA kapcsoló | A párosított mastertől | A párosított mastertől |

Az SD-s Slave nem másolja át a master korábbi történetét: saját előzménye a saját
adatgyűjtésétől épül. A két kijelző összesítője ezért eltérhet, például eltérő
indulási idő vagy kapcsolatkimaradás miatt.

Új, korábban be nem állított 10,1 hüvelykes kijelzőn böngészőben választható
a Slave szerep. A dedikált 4B első beállításakor ugyanígy csatlakozz a Wi-Fi-re
és erősítsd meg a **Slave** üzemmódot a kijelzett címen. A már üzemelő,
tartós szerepjelölővel rendelkező 3.x-es Slave és a korábban párosított,
felismerhető 4B megtartja a szerepét. A 10,1 hüvelykes panelen a visszahelyezett
SD konfigurációját a következő újraindításkor veszi át a firmware.
[BLE-párosítás és vegyes firmware-verziók](ble-data-seed.md)

A közvetlen inverter kapcsolati profilja, a koordináták és a heti sötétítési
programok a Master külön NVS-bejegyzésében vannak. Kártyacserekor ugyanazon a
kijelzőn megmaradnak. Kijelzőcserénél az új Masteren add meg őket az
**Inverter beállítások** fülön. [Közvetlen inverter](direct-inverter.md)

A közvetlen Loxone mód `loxone.json` konfigurációt is tárol. A jelszó eszközhöz kötött; másik kijelzőn újra megadandó. A diagnosztika az SD foglalt/teljes méretét és két tizedesre kerekített, százalékos telítettségi sávját mutatja (a kijelzőn és a webes diagnosztikán is). A kapacitás legfeljebb percenként frissül; induláskor rövid ideig még nem áll rendelkezésre.

## Csengőhangcsomagok

A Master `/sdcard/ringtones` könyvtára külön hangkészlet. A Loxone-oldali
ZIP-feltöltés első lépése a régi készlet és a `.new`/`.old` maradványok törlése;
ez nem érinti a témákat, konfigurációt vagy napi statisztikát. A telepítő
fájlonként ellenőriz, a hibás hangokat kihagyja és a végén felsorolja.
Megszakításkor a régi hangkészlet nem áll vissza. A Slave innen nem közvetlen
SD-hozzáféréssel, hanem a Master hitelesített médiaútján kapja a választott WAV-ot.
[A feltöltés működése és korlátai](intercom.md#csengőhang-betöltés-és-csomagcsere).
