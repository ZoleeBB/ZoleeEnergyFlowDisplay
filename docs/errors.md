# Hibakezelés

## Induláskori blokkoló hibák

Az alábbi hibák esetén a normál dashboard nem indul el:

- SD-kártya nem érhető el
- config.json hiányzik
- config.json hibás
- Wi-Fi inicializálás sikertelen
- NTP inicializálás sikertelen
- statisztikai tárhely nem inicializálható
- HTTP módban a fogadó webszerver nem indítható
- szükséges háttérfeladat nem indítható

A képernyőn a konkrét hiba látható, a részletes technikai log a soros konzolon érhető el.

## Wi-Fi kapcsolat megszakad

Azonnal teljes képernyős `Nincs Wi-Fi kapcsolat` állapot jelenik meg.

Újracsatlakozás alapértelmezés szerint 15 másodpercenként történik. Siker esetén a normál működés automatikusan folytatódik.

## MQTT kapcsolat megszakad

Külön MQTT kapcsolati hibaképernyő jelenik meg. Az újracsatlakozási idő alapértelmezés szerint 15 másodperc.

## Adatkimaradás

Ha az aktív MQTT- vagy HTTP-fogadó kész, de a konfigurált időn belül nincs érvényes csomag, teljes képernyős adathiányjelzés jelenik meg. A hibás csomag nem indítja újra az időkorlátot; érvényes csomagnál a felület automatikusan helyreáll.

## Futás közbeni SD hiba

A dashboard és az adatfeldolgozás tovább működik RAM-ból.

A képernyőn piros `SD-kártya hiba` figyelmeztetés jelenik meg. A firmware 1 percenként megpróbálja helyreállítani az SD kapcsolatot.

## Status = 4

Ez nem kommunikációs hiba. A dashboard a helyén marad, a rendszerállapot kártya erős piros jelzést kap.

## Watchdog

A firmware task watchdogot használ. A soros logban a reset oka is megjelenik, és a helyi információs weboldalon is lekérdezhető.

## RAM eseménynapló

A legutóbbi fontos események és hibák RAM-ban tárolódnak és a helyi webes információs oldalon megjelennek. Folyamatos SD logolás nincs.

MQTT indítási hiba a kommunikációs állapotban látszik; nem azonos a rendszer végzetes inicializálási hibájával. Az időjárási `null` érték nem hiba, csak a kapcsolódó kijelzés marad rejtve.
