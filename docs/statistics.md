# Statisztikai számítások

A pillanatnyi MQTT minták 5 másodperc körüli időközzel érkeznek. A napi energiákat a firmware teljesítmény-idő integrálással számolja.

## Integrálás

Két érvényes minta között trapézszabályt használunk.

Ha a két minta közötti idő nagyobb, mint a `statistics.max_integration_gap_seconds`, a kimaradt időszakra nem történik becslés. Alapérték: 30 másodperc.

Az éjfélt átlépő rövid mintaköz pontosan kettévágásra kerül a helyi 00:00 időpontnál, így az energia a megfelelő naphoz kerül.

## 5 perces grafikon

Egy nap 288 darab 5 perces slotból áll.

A grafikonokhoz az adott slotban érkezett teljesítményminták átlaga tárolódik:

- PowerPV
- PowerFull
- PowerGrid
- PowerBat

Az X tengely mindig 00:00-24:00. A jövőbeli időpontok üresen maradnak.

## Napi értékek

- Mai termelés: PowerPV integrál
- Mai fogyasztás: PowerFull integrál
- Hálózati vételezés: PowerGrid pozitív része
- Visszatáplálás: PowerGrid negatív részének abszolút értéke
- Akkumulátor töltés: PowerBat negatív részének abszolút értéke
- Akkumulátor kisütés: PowerBat pozitív része

## Önellátás

`(napi fogyasztás - napi hálózati vételezés) / napi fogyasztás * 100`

Az eredmény 0 és 100% közé van korlátozva.

## Megtakarított pénz

Helyben felhasznált PV energia:

`napi termelés - napi visszatáplálás`

Megtakarítás:

`helyi PV * electricity_purchase_price + visszatáplálás * feed_in_price`

Alapértékek:

- villamosenergia ár: 65 Ft/kWh
- betáplálási ár: 5 Ft/kWh

## CO2

`napi PV termelés * co2_factor_kg_per_kwh`

Alapérték: 0.381 kg CO2e/kWh.

## Fa-egyenérték

`megtakarított CO2 / tree_absorption_kg_per_year`

Alapérték: 22 kg CO2/fa/év.

## Tegnapi összehasonlítás

A kártyák nem a teljes tegnapi naphoz, hanem mindig a tegnapi nap ugyanazon befejezett 5 perces határáig hasonlítanak.

Példa: 14:37-kor a referencia 00:00-14:35.

Ha nincs tegnapi adat, az összehasonlítás nem jelenik meg. Ha a tegnapi referenciaérték nulla, százalék helyett mínusz jel jelenik meg.

## SD mentés

A RAM-ban gyűjtött statisztika alapértelmezés szerint 60 percenként kerül SD-re.

A mentés ideiglenes fájllal és biztonsági cserével történik. Induláskor a firmware szükség esetén a biztonsági fájlból próbál helyreállni.

A rendszer csak a mai és a tegnapi statisztikai napot tartja meg.
