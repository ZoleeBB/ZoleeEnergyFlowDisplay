# Automatikus OTA-frissítés diagnosztikája

A részletes üzenetek a Rendszernaplóban és a webes eseménynaplóban jelennek meg.
A napló RAM-ban körkörös, ezért hiba után érdemes rögtön lementeni vagy
lefényképezni. A bővített naplózást tartalmazó firmware telepítése szükséges;
a korábbi általános hiba okát utólag nem lehet visszanyerni.

## Szakaszok

| Jelölés | Művelet |
| --- | --- |
| release | A GitHub legfrissebb kiadásának HTTPS-lekérése és JSON-feldolgozása |
| SHA256-lista | A SHA256SUMS.txt lekérése és az adott firmware bejegyzésének keresése |
| firmware | A bináris letöltése és írása, fogadott/várt bájtokkal |
| képfejléc | ESP-képazonosító, projektazonosító és verzió ellenőrzése |
| flash-nyitás | A cél OTA-partíció megnyitása |
| SHA256 | A teljes bináris ellenőrzőösszegének ellenőrzése |
| képellenőrzés | A flash-be írt ESP-kép végleges ellenőrzése |
| statisztika | A statisztika mentése az újraindítás előtt |
| aktiválás | A következő indulás OTA-partíciójának kiválasztása |

A HTTPS-sorban a cél host, az átirányítási lépés és a 10 másodperces
kérés-időkorlát látható. A HTTP-válasz után a státuszkód és a Content-Length
jelenik meg; az ismeretlen hossz jelzése önmagában nem hiba.
A szöveges válasz teljes idejének korlátja 30 s, a firmware-é 180 s.
A firmware előrehaladása ritkított bejegyzéseket kap, nem minden TCP-csomag után.

## Hiba értelmezése

Például egy `OTA hiba [release]` sor azt jelenti, hogy a folyamat még a
kiadási adatok lekérésénél/feldolgozásánál állt meg. A `[SHA256-lista]` már
az ellenőrzőösszeg-fájlt, a `[firmware]` pedig magát a binárist jelenti.
A következő hálózati sor `errno`, `TLS`, `mbedTLS` és `cert` mezői az
ESP-IDF kliens által elérhető részleteket adják. Ha a kliens nem ad részletes
okot, ezek nulla értékűek is lehetnek; nem helyettesítjük őket feltételezett okkal.
A `kód` a jelzett API hibakódja, HTTP-hibánál a státusz, olvasásnál a visszatérési
érték. A hiba szövege különbözteti meg a hálózati, méret-, SHA256-, flash-,
SD-mentési és aktiválási problémákat.

A napló csak hostnevet tartalmaz, aláírt letöltési URL-t vagy annak tokenjét nem.
A tanúsítvány-ellenőrzés, a letöltési korlátok, a verzió- és képellenőrzés,
a SHA256-ellenőrzés, valamint a következő napváltáskor történő újrapróbálás
szabálya változatlan marad.

## Kézi próba és memória

A `/update` oldalon az **Automatikus frissítés most** gomb azonnal elindítja
ugyanazt a GitHub-ellenőrzést, amely normál esetben napváltáskor fut.
Az éjszakai kapcsoló kikapcsolt állapota mellett is használható, és nem
módosítja a beállítást. A HTTP 202 csak a háttérfeladat elindítását jelenti,
nem a sikeres telepítést; az eredmény a rendszernaplóban látható.
A végpont a beállított webes hitelesítést és saját kérésfejlécet igényli.
Futó frissítés/újraindítás mellett 409, Wi-Fi vagy szabad task-memória
hiányában 503 választ ad. A kézi feltöltéssel és az éjszakai ellenőrzéssel
közös kizárást használ.

A firmware **nem kerül teljes egészében RAM-ba**, és nincs SD-n tárolt
átmeneti bináris: 4096 bájtos pufferből közvetlenül az inaktív OTA-partícióba
írunk. Az első 288 bájt ellenőrzése flash-írás előtt történik.
RAM kell a HTTPS/TLS-klienshez, a release JSON-jához (legfeljebb 65537 bájtos
szövegpuffer és a feldolgozott JSON) és a SHA256-listához (16385 bájtos puffer).
A két szöveges válasz nem él egyszerre a firmware-letöltéssel.

Az `OTA RAM` sorban a szabad belső RAM, a legnagyobb összefüggő belső blokk,
a szabad PSRAM és annak legnagyobb összefüggő blokkja is szerepel, bájtban.
Az utóbbi azért számít, mert elegendő összes szabad memória mellett is
meghiúsulhat egy nagyobb, egybefüggő foglalás. A napló mindegyik HTTPS-kapcsolat
előtt, a szövegpuffer foglalása előtt és memóriahiba esetén is készül.

A GitHub aláírt átirányítási URL-je hosszabb lehet 1024 bájtnál. Az ESP-IDF
HTTP-kliense a GET kérősort egyetlen TX-pufferben állítja össze, ezért ennek
mérete most legalább az aktuális URL hossza + 128 bájt (minimum 1024).
A korábbi fix 1024-es puffer nem fedte le a letöltő által megengedett
URL-hosszt. Ez javított lehetséges hibaforrás; a konkrét eszközhibát az új
naplóval lehet bizonyítani. Az URL hossza és a TX-puffer mérete naplózott,
a signed URL tartalma továbbra sem kerül naplóba.
