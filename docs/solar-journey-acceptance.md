# Nap pálya – aktuális működés (v21)

A `templates/09-solar-journey` csomag a jóváhagyott nappali és éjszakai házfotót,
a külön mozgó napkorongokat, saját ikonokat és két LVGL-előnézetet tartalmazza.
Mindkét jelenet 1280×800 px-es; a kártyák geometriája azonos.

## Üzemállapot

A bal felső cím és a „Tiszta energia. Zöldebb holnap.” felirat alatt,
40×108 px kezdőponttal látszik az üzemállapot, ha eltér a normál működéstől.
Nappal és éjjel egyaránt 28 px-es szöveg, 1 px-es fehér árnyékkal,
szövegszélességhez igazodó, legfeljebb 424×52 px-es, lekerekített buborékban.
A jelzés a mozgó grafika fölött marad. A háttér nappal közel fedő fehér,
éjjel sötétkék; a színes keret és a felirat jól elkülönül tőle.
A figyelmeztetések pirosak:
**Felkészülés áramszünetre**, **Szigetüzem** vagy **Rendszerhiba**.
Az 5-ös állapot **Hálózat újra elérhető** felirata nappal sötétzöld, éjjel
világoszöld; a fehér árnyék megmarad.
Normál állapotban (`Status = 1`) sem a szöveg, sem az árnyéka, sem a buborék nem jelenik meg;
adatfrissítéskor azonnal követi az állapotváltást. Az angol felület angol státuszt mutat.
Ehhez a v21-es témacsomag és a `status_bubble` mezőt támogató firmware szükséges.

## Napjárás és sugárzás

Az opcionális `SunPosition` formátuma: `azimut#napmagasság#napkeltePerc#napnyugtaPerc`.
A [Loxone T1 bekötése](loxone.md) és a [JSON-protokoll](mqtt.md) közös az MQTT/HTTP módokban. Közvetlen Loxone módban négy külön numerikus állapot párosítható: azimut és magasság fokban, napkelte és napnyugta éjféltől eltelt percben; [beállítások](loxone-direct.md).
Adat nélkül a témalapozás átugorja a Nap pályát. A napkorong a helyi idő,
napkelte és napnyugta alapján, ívhossz szerint követi a háttérhez kalibrált
31 pontos nappali és 21 pontos éjszakai pályát. A nappali ív napnyugta előtti
szakasza sűrűbb, a háttér fényes vonalához illesztett pontokat használ; az első
16 pont és az éjszakai út változatlan. Az ívek a háttér részei, nem rajzoljuk újra őket.

A nappali sugárzási réteg a korong középpontjából, 1 px-es pontból tágul.
Erősebb, 110/255 induló fedettségű kitöltése és körvonala növekedés közben teljesen
elhalványul; külön sugárvonalak nincsenek. A réteg a nap PNG-je alatt van. A ciklus fixen 1,2 ciklus/s. A PV-teljesítmény csak a végső körátmérőt
szabályozza: 0–8 kW között arányos, 8 kW-tól a korábbi maximum.
A kitöltés minden végméretnél teljesen elhalványul; nulla termelésnél eltűnik.
Éjjel a korong nem pulzál, csak a mögötte levő holdudvar lélegzik 4,2 másodperces periódussal.
A holdudvar kitöltése 22–38/255, fényudvara 35–55/255 fedettségű. A sablon
minden adatfrissítése átadja az aktuális mintát az animációnak; nap/hold váltáskor
a fényudvar már az első képkockán a megfelelő méretet, színt és kitöltést kapja.

## Energiaáramlás és akkumulátor

A napelem–ház és a ház–hálózat út mellett a napelemtől röviden jobbra induló,
majd 90 fokban lefelé forduló út vezet az akkumulátorhoz. Napelemes töltéskor a napelemtől
az akkuhoz haladnak a pontok. Kisütéskor az akkutól felfelé, a csomópontban
jobbra fordulva a házig jutnak. Hálózati töltéskor a háztól balra, majd a
csomópontban lefelé fordulnak az akkuhoz. Vegyes töltéskor mindkét forrás
ága animálódik. Az aktív akkumulátorvonal is az érintett ágakra fordul: kisütésnél és hálózati
töltésnél a ház felé, napelemes töltésnél a PV felé, vegyes töltésnél mindkettőre. A PowerBat pozitív
értéke kisütés, negatív értéke töltés. A világoskék érték kisütést, a sötétebb kék töltést jelöl.
Az útvonal az adatfrissítéskor változhat, animáció közben csak a pontok mozognak. Inaktív fedettség 184/255,
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
A jelmagyarázat színazonos pontokat használ, a függőleges rácsvonalak egységesen 1 px-esek, a vízszintes skálavonalak is egyetlen pixelsorból állnak, erősebb tónussal.

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
induló pontját, kitöltését, rétegsorrendjét és fix ütemét, termelésarányos végméretét; kilenc SOC-határértéket,
a hálózati színeket, a kártyák szövegeit és a rács pixelszélességét.
Napnyugta előtt az utolsó teljes órát percenként végiglépteti: a napkorong
közepe legfeljebb 2 px-re térhet el a fotóról mért fényes ívtől. A nappali
végpont és az éjszakai kezdőpont egyezését is ellenőrzi mindkét témamódban.
A webes tesztek a mentett képek kiszolgálását, régi csomag üres keretét,
gyorsítótárazást és a biztonságos csomagfrissítést vizsgálják.

A napnyugta-illesztést a v18-as ZIP tartalmazza. A v19 geometriailag azonos,
friss előnézetet ad az erősebb holdudvarról. A holdudvar és a váltási hiba
javításához firmware-frissítés szükséges; ez a korábbi sablonnal is működik. A többi sablon
előnézete a saját új ZIP-je feltöltése után jelenik meg; a csomagok egyenként frissíthetők.

A nappálya témában az akku hőmérséklete egész °C-ra kerekítve látszik nappal és éjjel. Ez firmware-oldali formázás: a meglévő SD-s sablon is megkapja a javítást.
