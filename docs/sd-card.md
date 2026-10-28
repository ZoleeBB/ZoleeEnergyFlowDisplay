# microSD

FAT32 fájlrendszer szükséges; exFAT nem támogatott. A gyökérben legyen `config.json`, a kiválasztott [konfigurációs minta](configuration.md) alapján.

A tíz választható nézet könyvtárai a `/templates/<azonosító>/` alatt legyenek. Minden csomag saját `template.json`-t és az abban hivatkozott PNG-ket, valamint `preview-light.png` és `preview-dark.png` képet tartalmaz. A régebbi, előnézet nélküli csomag továbbra is betölthető; a webes keret csak a frissítés után kap képet. A kiválasztás `selection.*` fájljait a készülék kezeli. Másoláskor a csomag JSON-ját és képeit együtt frissítsd; [telepítési útmutató](templates.md).

A készülék a csomagok mellé `<alap>-<szélesség>x<magasság>.raw` segédfájlokat
készít a háttérben: ezek a már dekódolt és a megjelenítési méretre hozott képek,
tőlük lesz gyorsabb a témaváltás. Együtt a tíz gyári témánál nagyjából 45–90 MiB
helyet foglalnak, a `.tmp`/`.bak` állományokhoz hasonlóan a készülék kezeli őket,
és a csomag törlésével eltűnnek. Kézzel nem kell másolni vagy pótolni őket; ha
letörlöd, a kijelző a PNG-kkel dolgozik tovább, és a háttérben újra elkészíti
őket. [Részletek](performance.md).

A statisztikát a firmware maga hozza létre és 35 helyi naptári napig őrzi. Egy nap 288 ötperces időablakot tartalmaz. A `.tmp` és `.bak` fájlok helyreállítási állományok: működés közben ne töröld őket. Induláskor a legújabb érvényes revízió kerül betöltésre. [Adatformátum és számítás](statistics.md).

MQTT, HTTP és közvetlen Loxone módban induláskor az SD és a konfiguráció szükséges. Futás közben SD-hibánál a mérés és a RAM-beli statisztika tovább működik; a hiba a diagnosztikában látható. Újracsatlakoztatáskor a firmware ismét próbál menteni. Kártyacsere és kézi fájlmódosítás előtt kapcsold ki a készüléket.

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

Új, korábban fő kijelzőként nem azonosított eszköz SD nélkül automatikusan Slave
módban indul. Ismert fő kijelző SD-hibája nem vált szerepet. Korábbi fő kijelző
átállításához először SD-vel válassz Slave módot, indítsd el az új firmware-rel,
majd kikapcsolás után vedd ki a kártyát. Régi firmware-ről frissítve is szükséges
az első SD-s indulás a tartós szerepjelölő rögzítéséhez.

SD nélküli indulás után nincs háttérbeli kártyacsatolás; a behelyezett SD-t és
annak konfigurációját a következő újraindítás használja. Részletek és párosítás:
[BLE adatmegosztás](ble-data-seed.md#sd-nélküli-slave--csak-rendszer-téma).

A közvetlen Loxone mód `loxone.json` konfigurációt is tárol. A jelszó eszközhöz kötött; másik kijelzőn újra megadandó. A diagnosztika az SD foglalt/teljes méretét és két tizedesre kerekített, százalékos telítettségi sávját mutatja (a kijelzőn és a webes diagnosztikán is). A kapacitás legfeljebb percenként frissül; induláskor rövid ideig még nem áll rendelkezésre.

## Csengőhangcsomagok

A Master `/sdcard/ringtones` könyvtára külön hangkészlet. A Loxone-oldali
ZIP-feltöltés első lépése a régi készlet és a `.new`/`.old` maradványok törlése;
ez nem érinti a témákat, konfigurációt vagy napi statisztikát. A telepítő
fájlonként ellenőriz, a hibás hangokat kihagyja és a végén felsorolja.
Megszakításkor a régi hangkészlet nem áll vissza. A Slave innen nem közvetlen
SD-hozzáféréssel, hanem a Master hitelesített médiaútján kapja a választott WAV-ot.
[A feltöltés működése és korlátai](intercom.md#csengőhang-betöltés-és-csomagcsere).
