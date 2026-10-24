# Energiaáramlás tiltás – Loxone Otthon és kaputelefon

A **Beállítások → Általános → Energiaáramlás tiltás** kapcsoló alapból
kikapcsolt. Kizárólag **Loxone** adatfogadási módban engedélyezhető. Mentéskor
az eszköz szabályozottan újraindul. Más adatfogadási mód választása a webes
felületen kikapcsolja; kézzel írt, más móddal együtt engedélyezett konfigurációt
a validátor elutasít.

```json
{"DataReceptMode":"loxone","EnergyFlowDisabled":1}
```

Ez csak a kapcsolódó mezők szemléltetése: a szokásos Wi-Fi- és egyéb szükséges
konfigurációt továbbra is meg kell adni.

## Kijelző és témák

Aktív tiltáskor a Master alapnézete a lapozható **Loxone Otthon-kártyák**.
Nincs rendszer-, energia-, akkudiagnosztikai vagy fogyasztói nézet, illetve
energiaasszisztens. A balra/jobbra húzás csak az Otthon-oldalak között lapoz;
a bezárás nem vezet energiaoldalra. Felfelé húzással a rendszerdiagnosztika
továbbra is elérhető, bezáráskor visszatér az Otthonhoz.

A témakezelő csak a Loxone Otthont és annak erőforráscsomagját mutatja,
magyarázó figyelmeztetéssel. Energia-téma közvetlen API-kéréssel sem választható,
telepíthető vagy rendezhető át ebben az állapotban. A korábbi témák, sorrendjük
és kiválasztásuk megmaradnak, kikapcsolás után ismét elérhetők.

Ha vannak csempék, a megjelenítéshez a szokásos `loxone-home.zip` kell.
Hiányzó csomagnál beállítási útmutatás jelenik meg, energiaoldalra váltás nélkül.

**Ha az összes Otthon-oldalon együtt sincs egyetlen csempe**, a Master az
Otthon-oldal és erőforrásainak betöltése helyett fekete háttéren a mellékelt
[Loxone SVG-logót](../assets/loxone-wordmark.svg) és az „Energiaáramlás letiltve”
feliratot jeleníti meg. Ilyenkor a fényerő fix **10%**, nappal és éjjel is.
Egy másik oldalon lévő csempe már elegendő az Otthon-nézet engedélyezéséhez;
az egyes üres lapok nem törlődnek.

## Kaputelefon és fényerő

A [kaputelefon](intercom.md) változatlanul működik, az energiaadatoktól és az
Otthon-csomagtól függetlenül, elsőbbséggel. A csengetésre megnyíló videó,
kézi beszélgetés, kapunyitás és időzített bontás a meglévő szabályokat követi.

Üres Otthon esetén a hívás megjelenésekor a fényerő azonnal a beállított
nappali vagy éjszakai értékre vált. A kaputelefon bezáródásakor a logó és a
fix 10% tér vissza. Ez az átmenet nem lassú fényerő-animáció.
Otthon-csempék mellett a szokásos nappali/éjszakai fényerő működik.

## Loxone-beállítások és külön mentés

A webes felület külön menthető szakaszai:

| Szakasz | Aktív tiltáskor |
| --- | --- |
| Miniserver | Látható, külön mentés; a párosításokat nem módosítja |
| Adatpárosítás | Csak a **NightMode / Éjszakai mód** sor látható és menthető |
| Fogyasztók | Rejtett; korábbi beállításai megmaradnak |
| Otthon-kártyák | Látható; kizárólag az oldalakat és csempéket menti |
| Kaputelefon | Látható; saját mentés, külön videós hitelesítéssel |

Az Otthon-, fogyasztó- és kaputelefon-szakaszok normál módban is különállók.
Az egyik szakasz mentése nem menti a másik szakaszban még szerkesztett adatokat.
A böngésző háttérfrissítése sem írja felül a helyi szerkesztést.

Tiltáskor a Miniserver kapcsolati adatai és a NightMode párosítása szükségesek;
a többi energiaadat nem előfeltétel. A korábbi energia-párosítások változatlanul
megmaradnak a `loxone.json`-ban. A NightMode az Otthon hátterét és a hívás alatti
fényerőt vezérli; induláskor nappali az alapállapot, az első érvényes 0/1 értékig.
Kapcsolatkimaradásnál az utolsó ismert napszak megmarad.

## Slave-ek és frissítés

A párosított 10,1 hüvelykes és 720×720-as 4B Slave egyaránt fekete háttéren
a zöld logót és fehér „Energiaáramlás letiltve” feliratot mutatja. Ez az SD
nélküli Slave-re is érvényes. A Slave saját nappali/éjszakai fényerő- és
jelenlétszabályai megmaradnak; a fix 10% kizárólag az üres Otthonú Master szabálya.

Az Adat seed kapcsoló beállítása megmarad: ha eddig ki volt kapcsolva, a mód
nem engedélyezi automatikusan. Aktív seed mellett külön BLE módjelzés megy,
nem nulla teljesítményű energiaminta. Nem integrálunk belőle energiát vagy
hamis statisztikát. Belépéskor a Slave integrálási lánca is megszakad,
így rövid tiltási időn át sem számolunk energiát. Az időszinkron, PIN-es párosítás, Wi-Fi/OTA öröklés és
jelenlét működése megmarad. Friss módjelzés esetén a Slave OTA-ellenőrzése nem
követel energiamintát vagy SD nélküli energiatörténetet.

**A Master és az érintett Slave-ek firmware-ét is frissíteni kell a mód
bekapcsolása előtt.** A régi Slave nem ismeri az új BLE flaget. A három
csomagméret, UUID és protokollverzió megmarad; a kiegészítés az energia-csomag
korábban fenntartott bit3 jelzése. Teljes csomagpár és azonos sorszám szükséges.
Kapcsolatvesztés vagy 15 másodpercnyi csomaghiány a szokásos keresési hibajelzést
hozza vissza. Kikapcsolás után friss, érvényes energiaadatokkal tér vissza az
energianézet.

## Ellenőrzés

A C/JS regressziók a Loxone kizárólagosságot, a szakaszok izolált mentését,
rejtett adatok/jelszavak megőrzését, tiltott közvetlen témakéréseket, a valódi
BLE állapotgépet és a hibás módcsomagok elutasítását is ellenőrzik.
A tényleges LVGL Master-próba megvizsgálja az üres Otthon erőforrásolvasásainak
hiányát, a 10% → nappali/éjszakai → 10% PWM-váltást és a kaputelefon utáni
visszatérést. A 4B szimulátora a logós nézetből az energianézetbe visszatérést
is ellenőrzi. Fizikai panelen a hang/video és PWM viselkedésének kipróbálása
továbbra is szükséges.

[Fejlesztési terv és állapot](energy-flow-disable-plan.md)
