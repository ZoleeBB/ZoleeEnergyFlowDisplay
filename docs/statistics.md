# Energiastatisztika

A pillanatnyi kW értékekből a firmware időben integrált kWh mennyiségeket számol, két egymást követő érvényes minta között trapézszabállyal. A hálózat és akkumulátor előjelváltását külön szakaszokra bontja. Hosszú adatrésre nincs extrapoláció: a `max_integration_gap_seconds` alapértéke 30, tartománya 1–300 másodperc. A rövid, éjfélen átnyúló szakasz a két helyi nap között oszlik meg.

| Mérés | Pozitív | Negatív |
| --- | --- | --- |
| `PowerGrid` | Vételezés | Visszatáplálás |
| `PowerBat` | Akkumulátor leadása | Akkumulátor töltése |

A napi diagramok 288 ötperces időablakot használnak. 35 helyi naptári nap marad meg; a heti nézet hétfő–vasárnap, a havi nézet az aktuális naptári hónap. A mai nap részleges. A hiányzó időszak nem nulla fogyasztás. A tegnapi összehasonlítás azonos, lezárt ötperces időhatárig történik; hiányzó előzmény nem jelenik meg, nulla referencia esetén a százalék helyett kötőjel látszik.

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

## Órás napelemtermelés

A Nap pálya témához fejlesztett `solar_hourly` diagram a meglévő ötperces
slotokban integrált `pv_home_kwh`, `pv_stored_kwh` és `pv_export_kwh`
értékeket óránként összegzi. Az oszlop alsó zöld része helyi felhasználás és
tárolás; a sárga felső rész a hálózatba visszatáplálás. A hiányzó adatok nem
becsült napenergia-termelési értékek.
