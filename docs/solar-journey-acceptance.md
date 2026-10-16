# Nap pálya – aktuális működés (v14)

A `templates/09-solar-journey` csomag a jóváhagyott nappali és éjszakai házfotót,
a külön mozgó napkorongokat, saját ikonokat és két LVGL-előnézetet tartalmazza.
Mindkét jelenet 1280×800 px-es; a kártyák geometriája azonos.

## Napjárás és sugárzás

Az opcionális `SunPosition` formátuma: `azimut#napmagasság#napkeltePerc#napnyugtaPerc`.
A [Loxone T1 bekötése](loxone.md) és a [JSON-protokoll](mqtt.md) közös az MQTT/HTTP módokban.
Adat nélkül a témalapozás átugorja a Nap pályát. A napkorong a helyi idő,
napkelte és napnyugta alapján, ívhossz szerint követi a háttérhez kalibrált
21–21 pontos nappali/éjszakai pályát. Az ívek a háttér részei, nem rajzoljuk újra őket.

A nappali sugárzási réteg a korong középpontjából, 1 px-es pontból tágul.
Halvány kitöltése, körvonala és tizenkét finom sugara növekedés közben teljesen
elhalványul. A réteg a nap PNG-je alatt van. A ciklus sebessége a PV-teljesítménnyel
arányos: 8 kW-nál 1,2 ciklus/s, felette nem gyorsul; nulla termelésnél eltűnik.
Éjjel a korong nem pulzál, csak a mögötte levő holdudvar lélegzik 4,2 másodperces periódussal.

## Energiaáramlás és akkumulátor

A napelem–ház és a ház–hálózat út mellett a napelemtől röviden jobbra induló,
majd 90 fokban lefelé forduló út vezet az akkumulátorhoz. Napelemes töltéskor a napelemtől
az akkuhoz haladnak a pontok. Kisütéskor az akkutól felfelé, a csomópontban
jobbra fordulva a házig jutnak. Hálózati töltéskor a háztól balra, majd a
csomópontban lefelé fordulnak az akkuhoz. Vegyes töltéskor mindkét forrás
ága animálódik. A látható vonalak geometriája ettől nem változik. A PowerBat pozitív
értéke kisütés, negatív értéke töltés. A világoskék érték kisütést, a sötétebb kék töltést jelöl.
A vonalak állandó geometriájúak; csak a pontok mozognak. Inaktív fedettség 184/255,
aktív 250/255; a pontok sebessége 7 kW-nál maximális.

A hálózati teljesítmény importnál piros, exportnál citromsárga, áramlás nélkül
a napelemes érték színét kapja. A három teljesítmény és képaláírás közepe egyezik.
Az akkukártya mérete 307×78 px. A kWh-s összegzés alatt 129×6 px SOC-sáv van:

| SOC | Szín |
| --- | --- |
| 0–30% alatt | Piros |
| 30%-tól 50% alatt | Narancs |
| 50–70%, a 70%-ot is beleértve | Citromsárga |
| 70% felett | Zöld |

A tárolt energia `kapacitás × SOC / 100`; az `energy.battery_capacity_kwh`
webes beállítás 0 értékénél a kWh-s összegzés rejtve marad. A SOC egész százalék.

## Napi adatok

A felső órás termelésdiagram tényleges ötperces energiákból épül fel.
Zöld: **Helyben elhasznált és tárolt**; sárga: **Hálózatba visszatáplált**.
A jelmagyarázat színazonos pontokat használ, a rácsvonalak egységesen 1 px-esek.

Az alsó, változatlan méretű négy kártya: **Mai megtakarítás**, **Mai termelési érték**,
**Mai fogyasztás**, **Mai termelés**. Az utolsó a teljes napi PV-kWh-t mutatja.
Mind a négy kártyán egy sorban van a magyarázat és a tegnapi összevetés;
a magyarázat az ikon bal szélétől indul.

- Termelési érték: `production_kwh × electricity_purchase_price`.
- Megtakarítás: `(pv_home_kwh + pv_stored_kwh) × electricity_purchase_price + grid_export_kwh × feed_in_price`.
- Azonos lezárt, mindkét napon mért idősávokból készül az összevetés.
  Hiányos lefedettséget `~` jelöl. Hiányzó összevetés `—`; 0/0 alap `0%`,
  pozitív mai és nulla tegnapi érték `↑ új`. [Részletes statisztika](statistics.md).

## Ellenőrzés és frissítés

A valódi LVGL-teszt kilenc sablon 18 nézetét rendereli. Ellenőrzi a sugárzás
induló pontját, kitöltését, rétegsorrendjét és sebességhatárát; kilenc SOC-határértéket,
a hálózati színeket, a kártyák szövegeit és a rács pixelszélességét.
A webes tesztek a mentett képek kiszolgálását, régi csomag üres keretét,
gyorsítótárazást és a biztonságos csomagfrissítést vizsgálják.

A firmware mellett a v14-es téma ZIP-jét is frissíteni kell. A többi sablon
előnézete a saját új ZIP-je feltöltése után jelenik meg; a csomagok egyenként frissíthetők.
