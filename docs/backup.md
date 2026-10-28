# Beállítások mentése és visszaállítása

A webfelület **Mentés** menüpontja (`/backup`) egyetlen JSON-fájlba menti a
kijelző beállításait, és ugyanonnan vissza is tölthetők. A kijelző emellett a
saját flash-tárában is őriz egy példányt, így egy új vagy üres SD-kártyával
indulva magától visszaállítja a korábbi beállításokat.

## Mi kerül a mentésbe?

| Fájl az SD-n | Tartalom |
| --- | --- |
| `config.json` | Általános, hálózati, kijelző-, statisztikai, energia- és frissítési beállítások |
| `loxone.json` | Miniserver-kapcsolat és adatpárosítás (a jelenlét és a postaláda is) |
| `loxone-home.json` | Fogyasztók, Otthon-oldalak és -kártyák, kaputelefon |
| `templates/order.txt` | A témák sorrendje |
| `templates/selection.txt` | Az aktív téma |

Nem része a mentésnek a statisztika, a témák képei és a csengőhangok.
Ezeket a kiadási csomagokból (`-templates.zip`, `-ringtones.zip`) vagy a
Sablonok oldal GitHub-frissítésével lehet újratelepíteni.

## Jelszavak

- A **letöltött fájlban** a Wi-Fi-, MQTT- és webes adminisztrátori jelszó
  `null` értékű. Visszaállításkor a kijelzőn éppen tárolt jelszavak maradnak.
- A Loxone- és a kamerajelszó a fájlokban eleve az eszköz saját kulcsával
  titkosítva van. Ugyanazon a kijelzőn visszaállítva működik, másik kijelzőn a
  Loxone oldalon újra meg kell adni.
- A **belső tartalék** a teljes beállítást tartalmazza (az SD-kártya
  `config.json`-jához hasonlóan), mert csak így lehet egy új SD-kártyával
  kézi beavatkozás nélkül újra Wi-Fi-re csatlakozni.

## Visszaállítás fájlból

1. Válaszd ki a mentésfájlt, majd nyomd meg az **Ellenőrzés** gombot. Az eszköz
   minden részt ugyanazzal a validátorral ellenőriz, mint bootkor és a webes
   mentéskor, de még nem ír semmit.
2. A **Visszaállítás és újraindítás** csak akkor ír, ha minden rész hibátlan:
   egyetlen hibás rész esetén semmi sem változik. Fájlonként ideiglenes fájl és
   biztonsági másolat készül, így egy áramszünet sem hagy félig írt beállítást.
3. A statisztika mentése után az eszköz újraindul.

Részleges mentés is visszaállítható: csak a benne szereplő fájlok íródnak felül.

## Belső tartalék

- A flash korábban kihasználatlan, 7 MiB-os `storage` partícióját használja,
  fájlrendszer nélkül, két váltakozó, 1 MiB-os slotban. Mindig a régebbi slot
  íródik, a fejléc az adatok után kerül a helyére, így egy félbeszakadt írás
  nem rontja el az utolsó jó példányt.
- Csak akkor ír, ha a tartalom megváltozott (CRC32-összevetés): indulás után
  egyszer, illetve a Loxone- és Otthon-beállítások mentése után 15 másodperccel,
  összevonva. A témaváltás és a statisztika nem okoz flash-írást.
- **Automatikus visszaállítás:** ha induláskor az SD-n nincs `config.json`
  (új vagy üres kártya), a kijelző a belső tartalékból visszaírja a fájlokat,
  és normálisan elindul. Az eseménynapló ezt figyelmeztetésként jelzi.
- A `/backup` oldalon kézzel is frissíthető, illetve abból visszaállítható.

SD nélküli Slave-nél (belső NVS-beállítások) nincs mit menteni; a beállításokat
a mastertől kapja.

## API

| Végpont | Leírás |
| --- | --- |
| `GET /api/backup` | Letöltés (jelszavak nélkül) |
| `POST /api/backup/validate` | Ellenőrzés írás nélkül |
| `POST /api/backup/restore` | Visszaállítás és újraindítás |
| `GET /api/backup/flash` | Belső tartalék állapota |
| `POST /api/backup/flash` | Belső tartalék frissítése most |
| `POST /api/backup/flash/restore` | Visszaállítás a belső tartalékból |

Minden `POST` kérés a beállításoknál is használt `X-Zolee-Settings: 1`
fejlécet és a webes hitelesítést igényli.
