# Nap pálya – a 09-es téma működése

A 10,1″-es kijelzőre telepíthető gyári téma a Napot és a Holdat a napelemes ház feletti, illetve alatti íven mutatja. A jelenlegi SD-csomag verziója 27. Világos és sötét változata is 1280×800 képpontos, azonos kártyaelrendezéssel.

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

A kijelző helyi órája szerint **a napkelte percétől a napnyugta percéig a Nap jár a felső íven**. **A napnyugta percében megjelenik a Hold**, és a következő napkelte percéig az alsó íven halad. A Hold az éjszaka előrehaladását jelöli. Például 06:32-es napkelte és 19:47-es napnyugta mellett 06:32-kor már a Nap, 19:47-kor már a Hold látható.

A házfotót a világos vagy sötét témát kiválasztó éjszakai mód (`NightMode`) vezérli. Ha például a Loxone már 18:00-kor éjszakai módot küld, de a napnyugta 19:47, 18:00 és 19:47 között a sötét házfotó felett még a Nap mozog. A fényudvar az égitesttel együtt vált.

| Adatfogadási mód | A pályához használt adatok |
| --- | --- |
| MQTT vagy HTTP | Az opcionális `SunPosition` értékben a küldő adja át az azimutot, a napmagasságot és a helyi napkelte, napnyugta percét. [Loxone Pico C T1 bemenet](loxone.md#opcionális-nappálya-t1-szöveges-bemenet) · [Üzenetformátum](mqtt.md#opcionális-nappályaadatok) |
| Közvetlen Loxone | Négy numerikus állapotot kell párosítani a Miniserveren. [Mezők és mértékegységek](loxone-direct.md) |
| Közvetlen inverter | A Master a megadott koordinátákból és a helyi időből számítja ki a napadatokat. Ebben a módban a háttér is a napkeltéhez és a napnyugtához igazodik. [Hely és időzóna](direct-inverter.md#nappálya-időjárás-és-éjszakai-mód) |
| SD-kártyás 10,1″-es Slave | A párosított Master Bluetooth-on adja át a napadatokat és az időszinkront. [BLE-adatmegosztás](ble-data-seed.md) |

A 720×720-as Waveshare 4B a saját beépített Energiaáramlás nézetét használja. [4B kijelző](slave-waveshare-4b.md)

A gyári téma a háttérbe rajzolt vonalhoz illeszti a korongot. A v27-es csomagban a nappali pályát 50, az éjszakait 44 pont írja le. Ha a nappali időszak átnyúlik éjfélen, a kijelző ezt is követi. Ha rendelkezésre áll a naphelyzet, de nincs használható napkelte–napnyugta pár vagy helyi idő, a pozitív napmagasság Napot, a nulla vagy negatív napmagasság Holdat jelent. Hiányzó naphelyzetnél a korong rejtve marad, a témalapozás átugorja a 09-es témát. Az MQTT/HTTP üzenet ellenőrzésére a fenti protokollszabályok érvényesek.

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

Az LVGL-teszt a tényleges sablont rendereli. Ellenőrzi a sugárzás
induló pontját, kitöltését, rétegsorrendjét és fix ütemét, termelésarányos végméretét; kilenc SOC-határértéket,
a hálózati színeket, a kártyák szövegeit és a rács pixelszélességét.
Napnyugta előtt az utolsó teljes órát percenként végiglépteti: a napkorong
közepe legfeljebb 2 px-re térhet el a fotóról mért fényes ívtől. A nappali
végpont és az éjszakai kezdőpont egyezését is ellenőrzi mindkét témamódban.
A webes tesztek a mentett képek kiszolgálását, régi csomag üres keretét,
gyorsítótárazást és a biztonságos csomagfrissítést vizsgálják.

A Nap és Hold idő szerinti váltását a 10,1″-es kijelző firmware-frissítése adja. Ha a gyári v27-es téma telepítve van az SD-kártyán, a már telepített téma azonnal használható. A csomag már tartalmazza a `solar_day.png` és `solar_night.png` fájlokat. Korábbi, külön nap- és holdképpár nélkül készült 09-es témában a kijelző nappal aranyszínű, éjszaka kék korongot rajzol. Az egyedi témák a saját jelenetükben megadott `solar_disc` megjelenítést használják.

A napnyugta-illesztést a v18-as ZIP tartalmazza. A v19 geometriailag azonos,
friss előnézetet ad az erősebb holdudvarról. A holdudvar és a váltási hiba
javításához firmware-frissítés szükséges; ez a korábbi sablonnal is működik. A többi sablon
előnézete a saját új ZIP-je feltöltése után jelenik meg; a csomagok egyenként frissíthetők.

A nappálya témában az akku hőmérséklete egész °C-ra kerekítve látszik nappal és éjjel. Ez firmware-oldali formázás: a meglévő SD-s sablon is megkapja a javítást.
