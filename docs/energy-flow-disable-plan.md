# Energiaáramlás tiltás – fejlesztési terv

Alap: main / b0cb81e; fejlesztési ág: `feature/disable-energy-flow`.

## Elfogadott működés

- Új, alapból kikapcsolt „Energiaáramlás tiltás” kapcsoló, csak Loxone adatfogadásnál engedélyezhető.
- Aktív állapotban kizárólag a lapozható Loxone Otthon-kártyák jelennek meg; rendszer-, energia-, akkudiagnosztika- és fogyasztói nézet nem választható. A korábbi témaválasztás és sorrend megmarad.
- A kaputelefon továbbra is elsőbbséget kap, bezárásakor az Otthon-kártyákhoz térünk vissza.
- Az Adatpárosítás látható marad, de kizárólag NightMode párosításával. A többi korábbi párosítás megmarad, és nem feltétele a Loxone-kapcsolatnak.
- A Miniserver, adatpárosítás, fogyasztók, Otthon-kártyák és kaputelefon külön menthető szakasz. Egy szakasz mentése nem írja felül a többi mentett szakaszt, és nem menti a másutt még szerkesztett adatokat.
- A fogyasztói szerkesztő aktív tiltáskor rejtett. A kaputelefon és Otthon mindkét kapcsolóállásban látható.
- A témakezelő aktív tiltáskor csak a Loxone Otthont mutatja, figyelmeztetéssel és a szükséges erőforráscsomag telepítési lehetőségével.
- Hiányzó erőforráscsomagnál vagy üres Otthon-listánál beállítási útmutatás jelenik meg, energiaoldalra visszaváltás nélkül.
- A Slave-ek fekete háttéren a felhasználó által megadott zöld SVG-logót és fehér „Energiaáramlás letiltve” feliratot mutatnak. A módjelzés nem energiaadat, nem hoz létre hamis mérést vagy statisztikát. A párosítás, idő, jelenlét és frissítés működése megmarad.

## Kiegészítés: üres Otthon

Ha összesen egyetlen csempe sincs, a Master sem tölt Otthon-oldalt vagy annak
erőforrásait. Fekete háttér, a megadott SVG-logó és fix 10% fényerő látható.
Kaputelefon aktiválásakor az aktuális nappali/éjszakai beállított fényerő lép
érvénybe; bezárásakor visszatér a logó és az azonnali 10%. A kapcsoló tiltásán
kívül a korábbi működés marad érvényes.

## Lépések és állapot

- [x] Követelmények és NightMode/Slave működés tisztázása, jelenlegi kód áttekintése.
- [ ] Konfiguráció és webes beállításkapcsoló, Loxone kizárólagosság validálása.
- [ ] Szakaszonkénti mentés és szerveroldali adatmegőrzés, NightMode-only feldolgozás.
- [ ] Master Otthon-alapnézet, navigáció és témakezelő korlátozása.
- [ ] BLE módjelzés, mindkét Slave-kijelző logós nézete.
- [ ] C/JS regressziós tesztek, szimulátor, firmware build és méretellenőrzés.
- [ ] Felhasználói dokumentáció, README, feltöltés és PR.

## Ellenőrizendő határesetek

Új, energiaadatok nélküli Loxone-beállítás; részleges/hiányzó régi párosítás;
ki/be kapcsolás és újraindítás; kaputelefon aktív tiltás mellett; üres lap;
hiányzó/frissülő erőforráscsomag; közvetlen tiltott API-kérés;
szakaszok párhuzamos szerkesztése/mentése; mentési hiba;
BLE azonos sorszámú teljes csomagpár, elavult/hibás csomag, kapcsolatvesztés;
visszatérés normál energiaadatokra; SD nélküli és 720×720 Slave.
