# Desk Slave – 720×720-as látvány- és firmware-terv

Célhardver: [Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, SKU 31416](../firmware/desk-slave-waveshare-4b/README.md).
Ez a dokumentum az első látványtervezési lépést rögzíti; az új alkalmazás még
nincs implementálva. A feladat a fő Rendszer téma Energiaáramlás kártyájának
önálló, négyzetes megjelenítése és külön, virtuális tájat ábrázoló hátterének megtervezése.

## Elrendezés

A logikai vászon 720×720 pixel. A fejléc körülbelül 72 pixel magas, a diagram
alatta tölti ki a képet. A végleges natív feliratoknál a hosszú cím rövidíthető,
az időjárás opcionális mezői hiányzó adatnál elrejthetők.

| Elem | Tervezett hely és keret |
| --- | --- |
| Cím | Fejléc bal oldala, legfeljebb kb. 240 pixel |
| Időjárás | Fejléc középső/jobboldali része: ikon, hőmérséklet, szél ikon és km/h |
| Óra | Fejléc jobb széle, külön szélességgel HH:MM számára |
| Hálózat | Fent, kb. (360,145); neve és teljesítménye jobbra, kb. x=421-től |
| Akkumulátor | Bal szélen, kb. (94,380); alatta név, teljesítmény, egész SOC és akkuhőmérséklet |
| Napelem | Jobb szélen, kb. (626,380); alatta név és teljesítmény |
| Ház | Lent a zöldövezetben, kb. (360,625); neve és teljesítménye jobbra, kb. x=421-től |
| Hub | Kb. (360,380), az eredeti szürke villámos ikon, sokkal kisebb, kb. 30 pixeles átmérővel |

A külső ikonok körülbelül 88 pixelesek. Így a hub és a külső ikonok közt kb.
175–210 pixel hosszú, megszakítatlan út maradhat, a mozgó energiapontoknak is.
A koordináták látványtervezési irányértékek, a natív renderben ellenőrizendők.

A négy külső ikon és a hub ugyanazt az elrendezést és ikoncsaládot használja,
mint a 10,1 colos Rendszer kártya. Nincs napi összegző, grafikon vagy új funkció
a főképernyőn. A szövegek és a körök között maradjon elég hely a nagyobb értékeknek
és az ékezetes magyar címkéknek is. A W/kW váltás és az előjel nélküli számkiírás
a főprogram szabályait követi.

## Világos és sötét háttér

Világos változat: nyugodt kék égbolt, néhány puha felhő, a kép alsó részén zöld
rét, fák és a zöld környezetbe illesztett ház. A fejléc és a felső/mellékoldali
adatok tiszta égboltra, a ház jobb oldali adatai egyenletes világos rétre kerülnek.
A táj természetes, finoman térhatású, nem absztrakt hullámos háttér.

Sötét változat: ugyanaz a táj éjszaka, mélykék égbolt, visszafogott holdfény,
sötétzöld táj és a ház meleg ablakfénye. A feliratok világosak, az energiautak
színei elkülönülnek. A geometria és az adatpozíciók a nappalival azonosak.

Az olvashatóságot a táj részleteinek elhelyezése és a betűszínek biztosítják:
nincs háttérmaszk, homályosító fátyol, üvegpanel vagy szövegenkénti doboz.
Finom betűárnyék megengedett. A ház külön UI-ikonként a lenti tájba illeszkedhet;
ne legyen két, egymásra rajzolt ház.

Mindkét háttér önálló, szöveg és energiadiagram nélküli réteg lesz. Az ikonok, feliratok,
energiautak és mozgó pontok a háttér előtt, külön natív UI-elemek. A pontos
rétegrend: [megjelenési rétegek](../firmware/desk-slave-waveshare-4b/design/README.md).

## Példaadatok a látványterven

| Mód | Napelem | Ház | Akku | Hálózat |
| --- | --- | --- | --- | --- |
| Nappal | 7,0 kW | 4,0 kW | 2,0 kW töltés | 1,0 kW visszatáplálás |
| Éjjel | 0 W | 2,1 kW | 600 W leadás | 1,5 kW vételezés |

A példák teljesítménymérlege zárt. Az irányt az energiautak animációja és színe
jelzi; az értékek előtt nem szerepel mínuszjel. Az inaktív napelemes út éjjel is
látható marad, mozgó pontok nélkül. A példák szemléltető adatok.

## A firmware következő lépése

Az elfogadott látvány alapján külön ESP-IDF alkalmazás készül a
`firmware/desk-slave-waveshare-4b/` alatt. Csak Slave szerep: masterkeresés,
PIN-es párosítás, idő-/energia-/időjárás-szinkron, örökölt Wi-Fi/OTA-profil,
NVS-beállítások, PIN-pad és alapdiagnosztika. SD nem szükséges.

A board support a 4B saját ST7703/GT911 paneljét és GPIO-kiosztását használja.
A projektazonosító, firmware-fájlnév és OTA-cél elkülönül a 10,1 colos alkalmazástól.
A közös BLE kódot nem két eltérő másolatban tartjuk fenn. A tényleges függőségeket,
flashpartíciókat és C6-firmware-követelményt a port elkészítésekor rögzítjük és
fordítással ellenőrizzük.

A látványterv után natív 720×720-as renderrel ellenőrizendő a szövegillesztés,
az opcionális időjárás, a nappal/éjjel váltás és a PIN-pad. A rádió, érintés,
USB-C tápellátás és OTA a leszállított eszközön is próbát igényel.
