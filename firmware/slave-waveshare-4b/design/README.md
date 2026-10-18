# Megjelenési rétegek

A jóváhagyott tervből készült nappali és éjszakai táj. A két ház megjelenése
eltér: nappal kék, éjszaka meleg fénnyel világító ablakok. Az éjszakai napelem
mellől a napocska eltávolítva; a valódi Hold megmaradt. A statikus ikonok a
felhasználó jóváhagyásával a háttér részei.

| Réteg | Tartalom |
| --- | --- |
| Háttér | Táj és négy illusztráció, mérési adat nélkül: assets/day.png, assets/night.png |
| Energia | Körök, egyenes utak, három-három mozgó pont |
| Hub | Kis szürke villámos központ, a fő Rendszer téma PNG-je |
| Adatok | Natív fejléc, időjárás, óra, teljesítmény, SOC, akkuhőmérséklet |
| SOC | Vékony függőleges, alulról felfelé töltődő LVGL sáv az akku bal oldalán |
| Állapot | PIN-pad, keresés/lejárt adat, diagnosztika |

A két PNG natív 720×720 felbontású. Nincs háttérfátyol vagy
adatkártya. A PIN és a kérhető diagnosztika külön állapotfelület.

| Csomópont közepe | Nappal | Éjjel |
| --- | --- | --- |
| Hálózat | 360,130 | 360,130 |
| Akkumulátor | 130,360 | 130,360 |
| Napelem | 590,360 | 590,360 |
| Ház | 360,590 | 360,590 |
| Hub | 360,360 | 360,360 |

A két háttér illusztrációinak helye és mérete a nappali referenciához igazodik.
Minden ikon középpontja 230 pixelre van a hub közepétől. A körök sugara
egységesen 60 pixel, az energiautak hossza a hub közepétől a kör széléig
170 pixel. A geometria mindkét módban azonos.
A SOC sáv és az akku részletes felirata sem mozdul el módváltáskor.
Hálózat/ház adatai jobbra,
akku/napelem adatai alul. Hub: 42×42 pixel. A fő Rendszer téma közös
fázismodellje adja a folyamatos, teljesítményarányos animációt.

A képek a beépített ImageGen eszközzel készültek. Szerkesztési kérés: a mérési
szöveg, körök és energiautak eltávolítása az elfogadott tervből, a táj és ikonok
megtartásával; majd kizárólag az éjszakai napelem napocskájának eltávolítása.
