# Energiastatisztika

A pillanatnyi kW értékekből a firmware időben integrált kWh mennyiségeket számol, két egymást követő érvényes minta között trapézszabállyal. A hálózat és akkumulátor előjelváltását külön szakaszokra bontja. Hosszú adatrésre nincs extrapoláció: a `max_integration_gap_seconds` alapértéke 30, tartománya 1–300 másodperc. A rövid, éjfélen átnyúló szakasz a két helyi nap között oszlik meg.

| Mérés | Pozitív | Negatív |
| --- | --- | --- |
| `PowerGrid` | Vételezés | Visszatáplálás |
| `PowerBat` | Akkumulátor leadása | Akkumulátor töltése |

A napi diagramok 288 ötperces időablakot használnak. 35 helyi naptári nap marad meg; a heti nézet hétfő–vasárnap, a havi nézet az aktuális naptári hónap. A mai nap részleges. A hiányzó időszak nem nulla fogyasztás. A tegnapi összehasonlítás azonos, lezárt ötperces időhatárig történik; a két nap azonos, valóban mért slotjai számítanak. Az első órában legalább egy, utána legalább 12 közös slot szükséges. Hiányzó összevetésnél `—`, hiányos lefedettségnél `~` jelű közelítő százalék jelenik meg. Nulla mai és tegnapi referencia `0%`, pozitív mai és nulla tegnapi referencia `↑ új`.

## Forrásarányok és mutatók

A forráselosztás becslés: a PV először a házat, majd az akkumulátort látja el, a többlet export. Az akku leadását a házhoz rendeli; az akkumulátorból történő exportot nem modellezi. A fennmaradó házfogyasztást a hálózat fedezi. A hálózatról történő akkutöltés nem házfogyasztás. Az akku korábbi töltésének eredete nem követett.

A statisztikai mutatókban `helyi = PV→ház + akku→ház`:

- Önellátás: `helyi / fogyasztás × 100`, legfeljebb 100%.
- Saját felhasználás: `(PV→ház + PV→akku) / termelés × 100`, legfeljebb 100%.
- Vételi költség: `vételezés × vételi ár`; bevétel: `visszatáplálás × visszatáplálási ár`.
- Nettó költség: vételi költség mínusz bevétel.
- Megtakarítás: `helyi × vételi ár + bevétel`.

Nulla nevezőhöz 0% tartozik. Az aktuális konfigurált árak a teljes megjelenített előzményre érvényesek; nincs tarifatörténet.

A beépített rendszernézet korábbi összegző képletei eltérnek: önellátás `clamp((fogyasztás−vételezés)/fogyasztás×100,0,100)`, megtakarítás `max(0,termelés−visszatáplálás)×vételi ár + visszatáplálás×exportár`. A CO₂-becslés `termelés×CO₂-tényező`, a faegyenérték ezt osztja az éves faelnyelési tényezővel. Ezek szemléltető becslések.

## Tárolás

A v3 napfájlok integrált forrásenergiát és SOC-átlagot is tárolnak. A v1/v2 állományok olvashatók; forrásarányaik közelítettek, SOC-görbéjük hiányzik. A mentés alapértelmezetten 15 percenként történik; OTA firmware-frissítés és kézi újraindítás előtt is azonnali mentés fut. A mentés revíziós, `.tmp`/`.bak` helyreállítással, `fflush` és `fsync` használatával. SD-hibánál a RAM-beli gyűjtés tovább fut. Az opcionális időjárás nem befolyásolja az energiaszámítást.

## Autótöltés és napelemes arány

Közvetlen Loxone módban az `ev-charger` ikonú fogyasztók érvényes, kW-ra
átváltott összege kerül az energiamintába. A mai töltési energiát a firmware
csak két érvényes minta között integrálja, az ötperces és helyi éjféli
határokon szétvágva. Hiányzó/elavult adat vagy túl hosszú mintarés kimarad.
A ház `PowerFull` értéke már tartalmazza az autót, ezért az összeg nem adódik
hozzá még egyszer a fogyasztási mérleghez.

A közvetlen napelemes töltési teljesítmény becslése:
`autó_kW × (PV→ház / PowerFull)`, a közös forrásfelosztás alapján.
A napi százalék az integrált napelemes és teljes autótöltési energia hányadosa;
nem a pillanatnyi arányok átlaga. Az akkumulátor korábbi töltési eredete nem
követett, így az akkumulátorból fedezett rész nem számít napelemesnek.
Töltési energia nélkül a téma százalék helyett „–” jelet mutat.

A napi fájl opcionális `ev_charging_kwh` és `ev_charging_solar_kwh` mezői a
meglévő mentési ütemben íródnak, újraindítás után visszatölthetők, éjfélkor
új napi összeg indul. A régi fájlok olvashatók; a betöltő elutasítja a teljes
töltést meghaladó napelemes energiát. Ezek a mezők nem kerülnek a Slave
statisztikai transzferébe, HTTP/MQTT bemenetből pedig nem gyűlik autótöltés.
[Beállítás és megjelenítés](electric-car.md).

## Slave statisztikák

SD-kártyával a Slave ugyanígy, helyben integrálja a mastertől fogadott élő
mintákat, és a saját kártyájára ment. Az adott módban elérhető telepített témákat használhat (az Elektromos autó csak közvetlen Loxone módban választható);
a heti/havi nézetek a saját történetből dolgoznak. A master korábbi napjait nem
tölti le automatikusan. Kimaradáskor nincs becsült visszatöltés, és az ismételt
BLE heartbeat ugyanazt a mérést nem integrálja újra.

SD nélkül a master az egyetlen integrátor. A Slave 30 másodpercenként átveszi
a Rendszer témához szükséges mai 288 diagrampontot, összesítőket, tegnapi
összehasonlítást és a pénznem/ár/CO₂/fa referenciaértékeket. Csak a teljes,
ellenőrzött kép válik láthatóvá. Első szinkronig a napi érték `--`; kapcsolatbontáskor
vagy 90 másodperc után az utolsó jó, aznapi kép elavultságjelzéssel marad meg.
Másik masterre vagy új napra váltáskor a korábbi kép nem használható mai adatként.
A cache RAM-ban van, újraindításkor újra letöltődik, statisztikát nem írunk flashbe.
Nincs helyi heti/havi archívum. A transzferhibák az élő energia- és időadatok
fogadásától külön kezelődnek. [Protokoll és időkorlátok](ble-data-seed.md#a-diagramok-és-napi-összesítők-szinkronja).

## Órás napelemtermelés

A Nap pálya témához fejlesztett `solar_hourly` diagram a meglévő ötperces
slotokban integrált `pv_home_kwh`, `pv_stored_kwh` és `pv_export_kwh`
értékeket óránként összegzi. Az oszlop alsó zöld része helyi felhasználás és
tárolás; a sárga felső rész a hálózatba visszatáplálás. A hiányzó adatok nem
becsült napenergia-termelési értékek.

## Akku diagnosztika

A folytonos mintákból futó kapacitásbecslés és párosított energiahatásfok a
[tizedik téma leírásában](battery-diagnostics.md) szerepel. A normál napi energia
és a diagnosztikai szakaszok külön értelmezendők: napi betöltés/névleges arányból
nem következik akkumulátorromlás. A diagnosztikai előzmény a meglévő atomi napi
SD-mentés opcionális blokkja, az aktuális szakasz boot után újrakezdődik.
