# Önkéntes fejlesztői támogatás

A program használata ingyenes. Minden funkció, téma és kiadott frissítés
támogatás nélkül is elérhető. A befizetésért nem jár külön szolgáltatás vagy
jogosultság. Nincs próbaidő, aktiválás vagy támogatói állapot a kijelzőn.

## Megjelenés

A helyi webfelület főoldalán (`/`), a beállításoknál (`/settings`), a
sablonkezelőben (`/templates`), a Loxone-beállításoknál (`/loxone`), a
firmware-frissítésnél (`/update`) és a diagnosztikán (`/diagnostics`) ugyanaz
a visszafogott támogatási kártya jelenik meg a lap tetején. A meglévő hozzáférési
és adatfogadásimód-korlátok változatlanok. A magyar/angol oldalakon a megfelelő
nyelvű szöveg jelenik meg; a jelenleg magyar szerkesztőoldalak magyar kártyát kapnak.

A kártya bal oldalán a projekt teljes logója látható, mellette a szöveg és a
támogatási gombok. Legfeljebb 640 px széles nézetben a logó a tartalom fölé
kerül, a kártya és a kép a rendelkezésre álló szélességhez igazodik.
Az átlátszó, 240×124 px-es [webes PNG](../assets/branding/logo-web.png)
2337 bájt; helyi data URI-ként a generált HTML része, nincs új HTTP-kérés,
SD-olvasás vagy ESP-oldali képdekódolás. A generátor 4 KiB / 240×128 px
képméretkorlátot és nyelvenként 12 KiB-os HTML-korlátot ellenőriz.
A kiszolgálás közvetlenül a flashben tárolt állandóból történik, verempufferbe
másolás nélkül. A README nagyobb [fejlécképe](../assets/branding/readme-banner.png)
és az [eredeti felbontású logó](../assets/branding/logo.png) nem kerül a firmware-be.
A szerkeszthető [banner SVG](../assets/branding/readme-banner.svg) a mellette lévő
`logo.png` fájlt használja; a README a belőle renderelt, önálló PNG-t jeleníti meg.

A kártya a két engedélyezett szolgáltatóhoz vezethet:

- **Revolut.me:** általános profilhivatkozás, előre meghatározott összeg nélkül.
- **Ko-fi:** egyszeri, önkéntes támogatási oldal. A Ko-fi Free beállítást a
  szolgáltatónál kell választani; ezt a firmware nem tudja ellenőrizni vagy átállítani.

A fizetés kizárólag a külső szolgáltató oldalán történik. A hivatkozás új lapon
nyílik meg, a helyi eszköz URL-jének továbbadása és az eredeti oldal elérése nélkül.
A weboldal megnyitása nem tölt le külső képet, szkriptet vagy fizetési widgetet.
Nincs háttérben futó fizetésellenőrzés, támogatói adatbázis vagy pénzügyi titok
a firmware-ben. A kijelzőn nincs támogatási felirat vagy működésbeli különbség.

Az azonos 10.1-es firmware Master és Slave módban is ezt a webfelületet használja.
A külön 720×720-as 4B firmware jelenleg nem tartalmazza ezt az adminisztrációs
webkiszolgálót; a változás nem vezet be oda új hálózati szolgáltatást.

## A projektgazda hivatkozásai

A címek egyetlen központi fájlban állíthatók be:
[assets/support-links.json](../assets/support-links.json).

```json
{
  "revolut_me_url": "https://revolut.me/zoleebb",
  "ko_fi_url": "https://ko-fi.com/E1H8299KGN"
}
```

A projektgazda által megadott két címhez a webfelületen saját, zöld hivatkozásgomb
készül. A Ko-fi JavaScript-widgetjének betöltése nem szükséges; a README-ben a
Ko-fi hivatalos GitHub-gombja mutat ugyanerre a profilra. Egy cím kiürítésével
az adott gomb elrejthető; ha mindkét cím üres, az egész kártya rejtve marad.

Csak `https://revolut.me/` és `https://ko-fi.com/` alatti, egyetlen profilnevet
tartalmazó cím fogadható el, lekérdezés, összeg és fragment nélkül. A hivatkozások
nyilvános projektadatok, nem a végfelhasználó `config.json` beállításai.

A címek megadása után:

```sh
python3 tools/prepare_web.py
python3 tools/prepare_web.py --check
python3 tests/test_support_web.py
```

A generátor készíti a `main/app_support_links.h` fájlt és a meglévő weboldalak
beágyazott fejléceit; ezeket kézzel nem szabad szerkeszteni. A statikus kártyát
a kiszolgáló másolás és új heapfoglalás nélkül illeszti az oldalba.
Új linkhez új firmware-kiadás szükséges; az SD-s témacsomagok változatlanok.

## Pénzfogadás előtti teendők

A választott szolgáltató fiókfeltételeinek és a magyar adózási, illetve
bizonylatolási követelményeknek külön meg kell felelni. A „támogatás” elnevezés
és a Ko-fi Free csomag nem jelent automatikus adómentességet.
Személyes Revolut.me-fiók nyilvános fejlesztői támogatásra történő
használhatóságát a projektgazdának a szolgáltatóval kell tisztáznia.
