# Elektromos autó

A `11-electric-car.zip` egy új, 1280×800-as nappali/éjszakai energiaáramlási
téma. Közvetlen **Loxone** adatfogadási módban választható a Masteren;
MQTT, HTTP és Slave módban a lapozás átugorja, a webes aktiválás tiltott.
Az Energiaáramlás tiltás meglévő szabálya erre a témára is érvényes.

## Autótöltés

A Loxone weboldal **Fogyasztók** szakaszában válaszd az
**Elektromos autótöltő** (`ev-charger`) ikont minden olyan fogyasztóhoz,
amelynek mérése az autótöltés része. A kijelző a kiválasztott fogyasztók
szorzóval már kW-ra átváltott értékeit összeadja. Az **Elektromos autó**
(`electric-car`) ikon és az Otthon-kártyák nem vesznek részt az összesítésben.
Ugyanazt a teljes töltési mérést ne vedd fel több külön fogyasztóként,
mert az összegben minden konfigurált fogyasztó egyszer szerepel.

- **Nincs ilyen fogyasztó:** a téma választható marad; az autónál nincs
  felirat, érték vagy rajzolt energiaút. Az autó és a fekete kábel a háttérben marad.
- **Van töltő, de nincs fogyasztás:** 0 W, álló animáció. A fogyasztói
  képernyőhöz hasonlóan egy mérő 1 W alatti zajértéke 0 W.
- **Érvényes töltés:** az összegzett pillanatnyi teljesítmény W/kW formátumban
  látszik. Az animáció kizárólag a fali töltőtől az autó felé fut.
- **Hiányzó vagy érvénytelen töltőadat, megszakadt kapcsolat:** „Nincs adat”;
  nincs részösszegből képzett félrevezető fogyasztás és nincs töltési animáció.

**A ház teljes fogyasztása továbbra is a `PowerFull`.** Az autótöltés ebből
nem vonódik le, és a napi házfogyasztáshoz sem adódik hozzá másodszor.
A napelem, a hálózat és az akkumulátor energiaútja az inverterhez kapcsolódik;
a fekete töltőkábelt követő új út az autótöltés részértékét mutatja.
A 2-es témaverzióban két összefüggő Bézier-szakasz követi a kábel középvonalát;
a nappali és az éjszakai képhez külön illesztett geometria tartozik.

## Kártyák (4-es témaverzió)

- A fejléc (cím, téma neve, dátum, óra, időjárás) a többi mezőhöz hasonló
  áttetsző sávot kapott, így nappal is jól olvasható.
- A **Napelem termelés** kártya a tető jobb oldalán áll.
- A hálózati kártya címe az áramlás irányát mutatja: „Hálózati vételezés”
  vagy „Hálózati visszatáplálás”; áramlás nélkül „Hálózat” (`grid_title`).
- Az **Akkumulátor** kártya jobb oldalán az akku hőmérséklete látszik.
- Az **Autótöltés** kártya jobb oldalán kisebb betűvel a mai, autóba töltött
  energia: **Ma összesen** (`ev_charging_today`, kWh).

### Mai autótöltési energia

A Loxone-kliens az 5 másodperces energiamintához hozzáadja az érvényes
autótöltési összeget (belső `EvChargingPower` mező, kW). A statisztika ezt a
többi energiával azonos módon, két egymást követő érvényes minta között
trapézszabállyal integrálja, ötperces és éjféli határon vágva. Hiányzó vagy
elavult töltőadat, illetve a beállított maximális mintarésnél hosszabb kimaradás
szakaszát nem számolja, így nincs becsült vagy kitalált energia.

Az összeg a napi statisztikai fájl opcionális `ev_charging_kwh` mezőjébe kerül
a meglévő mentési ütemben, így újraindítás után is megmarad, és éjfélkor nulláz.
Külön SD-írás nincs. A régebbi firmware a mezőt figyelmen kívül hagyja, a régi
napi fájlok változatlanul betölthetők. A mező nem része a házfogyasztás
energiamérlegének, és a Slave-ek felé küldött statisztikának sem.
HTTP/MQTT csomagban az azonos nevű mezőt a kijelző figyelmen kívül hagyja.

## Telepítés és webes szerkesztés

Frissítsd a Master firmware-ét, majd telepítsd a `11-electric-car.zip`
csomagot a **Sablonok** oldalon. A firmware-frissítés önmagában nem másolja
az új háttereket az SD-kártyára. A többi gyári téma megmarad.

A Fogyasztók és az Otthon-kártyák szerkesztőjében minden beállításmező
látható marad. A nagyobb ikon külön oszlopba kerül, a mezők tömör rácsban
jelennek meg, a hosszú Loxone-választók teljes szélességet kapnak.
A meglévő zöld címkék, színek, világos szövegek és mezőfeliratok megmaradnak.
Az ikonválasztó magyar megnevezés szerint, ékezethelyes ABC-sorrendben rendezett.

A 720×720-as Waveshare 4B külön Slave-firmware nem kap új oldalt ebben a
fejlesztésben: ez a funkció közvetlen Loxone módhoz kötött Master-téma.

[Fejlesztési terv és ellenőrzési állapot](electric-car-development.md).
