# Nap pálya – véglegesített megjelenési és számítási szerződés

A 1280×800-as felület nappali és éjszakai jelenete azonos geometriát használ. A képháttér és az összes adat-/grafikusréteg elkülönül. A napkelte (104×51), napnyugta (108×51), napmagasság (148×58) panelek a jóváhagyott látványtervnél is kisebb, áttetsző kártyák.

## Megerősített követelmények (2026-10-15)
- Nappali jelenet: ugyanaz az elrendezés, világos üvegszerű kártyák és olvasható sötét feliratok. Éjszaka: sötét üvegszerű kártyák, világos feliratok.
- A napkorong az íven a napi napkelte/napnyugta időpontok alapján halad; éjszaka a **horizont alatti** kék szaggatott íven mozog halvány kék fényben, **pulzálás nélkül**.
- Napközben a fényudvar napelem-teljesítményfüggő, 8 kW-nál telítődik.
- Jobb felső fejléc: dátum, nagy óra, opcionális időjáráskép és állapot, hőmérséklet, szél. Nincs külön opak fejlécdoboz.
- Jobbra a 24 órás valós termelésdiagram, alatta az animált napelem–háztartás–hálózat áramlás és egy alsó akkumulátorsáv.
- Egyetlen Hálózat mező: vételezés piros, visszatáplálás citromsárga.
- Akkumulátor: egész százalék, tényleges energia / maximum kWh, hőmérséklet és pillanatnyi teljesítmény. A kapacitást a webes `energy.battery_capacity_kwh` beállítás adja. Hiányzó kapacitás esetén a kWh-s összegzés rejtve marad.
- Az alsó négy kártya rögzített mérettel és sorrendben: **Mai megtakarítás**, **Mai termelési érték**, **Mai fogyasztás**, **Mai visszatáplálás**. Jobb alsó sarkukban apró, azonos napszakhoz tartozó tegnapi százalékos összevetés.
- `Mai termelési érték = production_kwh × electricity_purchase_price`.
- `Mai megtakarítás = (pv_home_kwh + pv_stored_kwh) × electricity_purchase_price + grid_export_kwh × feed_in_price`. A hálózatból akkuba töltés nem szerepel a `pv_stored_kwh` részben.
- Az összehasonlítás mai és tegnapi **azonos lezárt ötperces idősávból** származó statisztikával történik; ha tegnapi alapadat vagy az osztás nevezője nulla, `—` látható.

## Kiadás előtti feltételek

A **V6** nappali és éjszakai háttér, a mozgó napkorong és a teljes 25 darabos ikoncsomag az `09-solar-journey` témakönyvtárban szerepel. A két üzemmód közös geometriát használ, az ívek már a háttérbe vannak beépítve; a program csak a napkorongot mozgatja.

A C sablonvalidátor, a böngészős ZIP-validátor, a firmware build, a Windows-szimulátor és az LVGL-előnézet CI-próbája legyen sikeres. A valós **1280×800-as** kijelzőn a napkorong pályáját és a kártyák olvashatóságát külön is ellenőrizni kell. A PR-t ennek igazolásáig nem szabad automatikusan egyesíteni.
