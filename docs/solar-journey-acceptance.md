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

## V7 – háttérből kalibrált mozgás és részleges tegnapi adatok

A helyesnek elfogadott 1586×992-es éjszakai/nappali fotók 1280×800-ra optimalizált PNG-változatának felrajzolt fényíve 21–21 abszolút, háttérhez illesztett mérési pontot kapott. A nappályát **nem** rajzolja meg újra a firmware, csak az előre meghatározott pontokon, ívhossz szerint mozgatja a napkorongot. A napkelte és napnyugta panelek 18 pixellel jobbra és 14 pixellel lejjebb kerültek.

A napelemtől a házhoz, valamint a hálózat és a ház, illetve az akkumulátor és a ház között mozgó pontok kisebbek (6 px). A hálózatból történő vételezéskor hálózatból a ház felé, akkumulátorból leadáskor akkumulátorból a ház felé haladnak. Az alsó akkumulátorsáv látható keretet kapott.

A tegnapi összehasonlításban azonos, az utolsó lezárt ötperces időszakig tartó 0:00 utáni slotokat használjuk. Ha a vizsgált időszak kevesebb mint 85%-ában áll rendelkezésre használható napi minta valamelyik oldalon, százalék helyett `Részleges` jelenik meg. A két háttér PNG-t és a 6 optimalizált új ikont a `NapPalya-V7-kesz-sablon.zip` csomagból kell a GitHubon lecserélni, mielőtt a fájlellenőrző CI teljesen zöldre vált.

## V8 – pontosabb ív és kisebb áramláspontok

A napkorong továbbra is kizárólag a háttérfotóra illesztett pályapontok mentén mozog, külön vonalat a program nem rajzol. A 21 pontos éjszakai ív középvonala a jóváhagyott referencián látható kék ívhez igazítva 612 px mélyre került (1280×800). A felső és alsó ív találkozópontjai egyeznek, napkeltekor és napnyugtakor nincs pozícióugrás.

Az Energiaáramlás panel három útvonalán két-két, 6 pixeles pont mozog, a hálózatimport az oszloptól a ház felé, az akkumulátorkisütés az akkutól a ház felé. Az akkumulátoros Bézier-ív a külön keretezett alsó akkusáv peremén végződik, nem fut alatta.

A háttérképek és az ikonok fájljai **nem módosultak**. A telepítő és a böngészős validátor megenged legfeljebb 2,5 MiB-os egyedi PNG-t és 5 MiB összesített tömörített képadatot; a 8 MiB ZIP és 12 MiB dekódolt memóriahatár változatlan.

## V9 – apró megjelenési javítások

A Napkelte információs kártya 92 px-rel balra, a Napnyugta 38 px-rel jobbra került, mindkettő a napív mellett, azon kívül marad. Az Energiaáramlás panelen az inaktív vonalak 112/255, az aktívak 240/255 fedettségűek; a három útvonal 3 px vastagságú. Az akkumulátorsávot a hőmérő előtt 7 apró, a keret színével azonos szakaszból álló szaggatott vonal tagolja. A holdkorong képe változatlan: csak a mögöttes fényudvar 9 másodperces, néhány pixeles animációt kapott.

Minden tegnapi összehasonlítás 0:00-tól az utolsó lezárt ötperces idősávig tart. A közös rendszerértékek és a Nap pálya öt százalékos mezője egyaránt 'Részleges' feliratot mutatnak, ha a statisztika részleges. A napelemes ikon PNG-fájlját a felhasználó töltötte fel; itt csak az ellenőrzőösszeget frissítettük.

## V10 – valós tegnapi összevetés, energiapontok, hold és akkumulátor

A 00:00-tól az aktuális **utolsó lezárt ötperces slotig** tartó időszak az összehasonlítás alapja. A teljes statisztika továbbra is változatlanul tárolódik. Az összevetéshez a két nap **azonos időpontú és mindkét oldalon valóban mért slotjai** szolgálnak; a hiányzó időablakok nem számítanak nulla energiafogyasztásnak. Minimum 12 közös ötperces slot (az első órában legalább egy) szükséges. A tegnapi adatok meglétét ettől függetlenül nyilvántartjuk, de ha kevesebb közös adat van, nem írunk félrevezető százalékot. Teljes lefedettségnél pontos százalék, hiányos lefedettségnél **≈** jelű közelítő százalék jelenik meg az összes rendszer- és Nap pálya kártyán, beleértve az akkumulátor töltését/kisütését. A `tests/test_stats_comparison.c` generált teljes, részleges, hiányzó, nullás és korábbi verzióból származó statisztikákkal, egy valós C tesztben ellenőrzi ezt a számítást.

Az Energiaáramlás panel három útvonalának inaktív fedettsége 184/255, aktívé 250/255, az inaktív fényudvar csak 28/255. Az akkumulátorhoz vezető kék Bézier görbe a felső elágazásból indul, és a külön keretezett akkusáv fölött ér véget, akkor is látszik, ha épp nincs áramlás. A SOC melletti kis érték az akku pillanatnyi teljesítménye: **világoskék kisütéskor**, **sötétebb kék töltéskor** (az előjel nélkül, W/kW formátumban). A hold korongja változatlan: csak a körülötte lévő fényudvar pulzál finoman, 4,2 másodperces ciklussal és folytonos áttetszőség-változással. A képfájlok változatlanok.
