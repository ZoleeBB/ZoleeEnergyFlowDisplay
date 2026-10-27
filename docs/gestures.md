# Kezelés és gesztusok

Ez a lap összefoglalja, milyen érintés és húzás mit csinál a kijelzőn.
A húzás legalább 50 pixeles, egy irányba tartó mozdulat. A kis remegést
a firmware nem veszi húzásnak, a lassú húzás viszont ugyanúgy érvényes.
Egy húzásnak induló érintésből nem lesz gombnyomás.

## 10,1 colos kijelző (Master és SD-s Slave)

| Mozdulat | Hol | Mit csinál |
| --- | --- | --- |
| Húzás balra | Energiatémák | A következő témára lép. A lista körbeér, a Rendszer nézet mindig az első. |
| Húzás jobbra | Energiatémák | Az előző témára lép. |
| Húzás felfelé | Bármelyik energiatéma | Megnyitja a diagnosztikát. A jobb felső **X** zárja be. |
| Húzás lefelé | Master, Loxone mód | Megnyitja az Otthon-kártyákat (ha a `loxone-home.zip` telepítve van és van beállított oldal). |
| Húzás balra/jobbra | Otthon-kártyák | Lapoz az Otthon-oldalak között. Az oldalakat a jobb felső **X** zárja be. |
| Érintés a házon | Master, Loxone mód | Megnyitja a ház külön mért fogyasztóit. |
| Érintés a kártyán | Otthon-kártyák | Kapcsoló: be/ki; pillanatnyi gomb: egy impulzus. |
| **X** az asszisztens sávján | Energiatémák | Elrejti az energiaasszisztens javaslatát (30 percre vagy az adott éjszakára). |
| Érintés a postaláda-jelen | – | Nincs művelete; a jel addig látszik, amíg a Loxone-állapot 1. |

Néhány nézet nem lapozható:

- SD nélkül csak a beépített Rendszer nézet érhető el, a vízszintes húzásnak nincs hatása.
- A PIN-pad és a diagnosztika alatt a téma nem vált.
- A téma betöltése közben (pörgő körös oldal) a vízszintes húzás nem vész el: a
  kérés sorba kerül, és a betöltés után azonnal lefut. Az oda-vissza húzás
  kioltja egymást, így a kiinduló témánál maradunk. Ugyanígy nem vész el a
  húzás akkor sem, ha a katalógus éppen foglalt (SD-olvasás vagy -írás).
- Energiaáramlás tiltás módban csak a felfelé húzás (diagnosztika) és az Otthon-oldalak lapozása működik.
- A Nap pálya téma csak érvényes `SunPosition` adat mellett szerepel a lapozásban.

### Sötét kijelző felébresztése

- **Slave éjszakai sötétítéssel:** az első érintés csak felébreszti a kijelzőt,
  nem kattint és nem lapoz. A következő érintés már a szokásos módon működik.
- **Master jelenlétvezérléssel** (Loxone mód, ha a *Jelenlét* adat párosítva van):
  éjszaka, amikor a jelenlét 0 / false, a kijelző teljesen elsötétül. Érintésre a beállított
  éjszakai fényerőre világosodik. Az ébresztő érintés itt sem kattint. A kijelző a
  *Kijelző → Ébrenlét érintés után* ideig marad világos, utána újra elsötétül.
  Ha a jelenlét 1 / true értékre vált, a kijelző azonnal az éjszakai fényerőre világosodik.
  Ha nincs párosított jelenlét-adat, minden a korábbiak szerint működik.

### Kaputelefon

A kaputelefon teljes képernyős nézetében a gombok (beszélgetés, kapunyitás, bezárás)
érintéssel működnek, a húzásoknak nincs hatása. Részletek: [Kaputelefon](intercom.md).

## Waveshare 4B Slave (720×720)

| Mozdulat | Mit csinál |
| --- | --- |
| Húzás balra vagy jobbra | Vált az energiaáramlás és a **Mai összesítő** nézet között. |
| Érintés a jobb felső sarokban (óra) | Megnyitja/bezárja a Slave diagnosztikai paneljét. |
| Első érintés sötét kijelzőn | Csak felébreszt, nem kattint és nem lapoz. |

A Mai összesítő a mastertől kapott napi adatokat mutatja: termelés, fogyasztás,
vételezés, visszatáplálás, önellátás és a napi pénzügyi összegek. Első szinkronig
és régi adatnál `—` látszik. A párosításhoz szükséges PIN-pad gombjai érintéssel működnek.

## Webes felület

A kijelző gesztusai helyett a böngészőből is elérhető minden beállítás:
`http://zolee-energy-flow-display.local/` vagy az eszköz IP-címe.
A felső menüből érhető el a Beállítások, a Loxone, a Sablonok, a Mentés és
visszaállítás, a Firmware-frissítés és a Diagnosztika.
