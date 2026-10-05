# Közvetlen Loxone HTTP POST adatküldés

**Állapot:** 2026-10-05-én elfogadott fejlesztési igény és előkészített minta. A dokumentum az új HTTP-fogadó implementálása után alkalmazható a kijelzőn; a jelen módosítás nem kapcsol be HTTP-adatfogadást a firmware-ben. A minta célkörnyezete Miniserver Gen 2, Loxone Config és firmware 17.x; a tényleges Miniserveren végzett próba még szükséges.

## Adatút

A Program blokk I1–I12 bemeneteiből a Pico C program öt másodpercenként teljes JSON body-t készít. A Txt1 kimenetet a HTTP virtuális kimeneti parancshoz kell kötni. A Miniserver közvetlenül a kijelzőnek küldi a POST-kérést; MQTT broker és közvetítő nem szükséges.

Minta: [loxone-energy-http-post.picoc](../examples/loxone-energy-http-post.picoc).

## Kijelző konfigurációja

A meglévő SD `config.json` gyökerébe az alábbi mezők kerülnek. Ez csak kiegészítési részlet, nem teljes konfiguráció:

```json
{
  "DataReceptMode": "http",
  "http": { "data_timeout_seconds": 30 }
}
```

A Wi-Fi, idő, kijelző és többi beállítás megmarad. A konfigurációt újraindításkor olvassa be a firmware. HTTP módban az `mqtt` szakasz elhagyható. Ha a `DataReceptMode` nincs megadva, továbbra is MQTT a választott mód.

Az adatfogadáshoz **nem kell felhasználónév, jelszó vagy API-kulcs**, akkor sem, ha az OTA-oldalon engedélyezve van a Basic Auth.

## Program blokk bemenetek

| Bemenet | Mező | Egység/jelentés |
|---|---|---|
| I1 | PowerGrid | kW; pozitív vételezés, negatív visszatáplálás |
| I2 | PowerBat | kW; pozitív kisütés, negatív töltés |
| I3 | PowerPV | kW, PV-termelés |
| I4 | SocBat | % |
| I5 | PowerFull | kW, házfogyasztás |
| I6 | Temperature | °C, külső hőmérséklet |
| I7 | WindSpeed | km/h |
| I8 | HasSunRadiation | 0/1 |
| I9 | HasPrecipitation | 0/1 |
| I10 | Status | 1: normál; 2: felkészülés; 3: szigetüzem; 4: hiba |
| I11 | NightMode | 0/1 |
| I12 | BatteryTemperature | °C |

A scriptbe egyetlen Program blokkon belül másold be a mintafájlt. A Txt1 a JSON-t, az Etxt a Pico C értelmező hibaüzenetét adja. Az I/O-indexek a kódban nullától indulnak, ezért I1 = `getinput(0)`.

## Virtuális kimenet beállítása

1. A Perifériában hozz létre egy **Virtuális kimenetet**; címe `http://KIJELZO_IP`, például `http://192.168.1.80`. Ne kerüljön felhasználónév/jelszó a címbe. Stabil IP-címhez használható a meglévő statikus beállítás vagy DHCP-foglalás.
2. Alatta hozz létre egy **Virtuális kimeneti parancsot**, például `Energy state POST` néven.
3. Állítsd be az alábbi táblázatot, majd kösd a Program blokk **Txt1** kimenetét a parancshoz.

| Tulajdonság | Érték |
|---|---|
| Parancs BE állapotban | `/api/energy` |
| HTTP header BE parancshoz | `Content-Type: application/json` |
| HTTP body BE parancshoz | `<v>` |
| HTTP mód BE esetén | `POST` |
| Alkalmazás digitális kimenetként | Kikapcsolva |
| Parancs KI állapotban | Üres |
| HTTP header/body KI parancshoz | Üres |
| Első ismétlés [s] | `0` |
| Ismétlések közötti távolság [s] | `0` |
| HTTP válasz mentése | Üres |

A `<v>` helyére a Txt1 teljes szövege kerül. A body mezőbe ne kerüljön extra idézőjel, `GET` parancs vagy teljes HTTP fejléc. A JSON számok tizedesponttal készülnek; a kijelző magyar tizedesvesszős megjelenítése külön formázási lépés.

## Változatlan mérés és hibafelismerés

A script felváltva egy szóközt helyez a nyitó `{` után, illetve elhagyja azt. Ettől a JSON értelmezett tartalma változatlan, de a Txt1 szövege minden ciklusban különbözik. Így a szövegváltozásra küldő virtuális kimenethez új érték jut állandó mérés mellett is. A tényleges küldési periódust Gen 2 / 17.x alatt ellenőrizni kell; ez nem minősül már elvégzett Miniserver-tesztnek.

Az automatikus kimeneti ismétlés maradjon kikapcsolva. Ellenkező esetben a Miniserver egy leállt Program blokk utolsó mérését is újraküldhetné, és a kijelző nem érzékelné az adatforrás leállását.

Érvényes adat nélkül 30 másodperc után a kijelző főszövege **„Nem érkezik HTTP adat”**. Új érvényes POST hatására automatikusan visszatér a dashboard. Hibás JSON nem nullázza az időzítőt. Wi-Fi kiesése külön Wi-Fi hibaoldalt eredményez.

## Ellenőrzés a HTTP-fogadó implementálása után

Windows PowerShellből, a repository gyökeréből:

```powershell
curl.exe -i -X POST "http://192.168.1.80/api/energy" -H "Content-Type: application/json" --data-binary "@examples/mqtt-payload.sample.json"
```

Siker: HTTP `200`, body `{"ok":true}`. A JSON mérési séma azonos az MQTT-minta sémájával. Böngészős címsorban történő megnyitás GET-kérés, ezért nem adatküldési próba: HTTP módban `405` várható.

Hardveres próba: legalább egy percig változatlan bemenetek mellett is érkezzen öt másodpercenként POST. Ezután a Program blokk adatküldését leállítva 30 másodperc után jelenjen meg az adat-timeout; újraindítva a küldést térjen vissza a dashboard. Külön ellenőrizendő az Etxt, a negatív hálózati/akkuteljesítmény, az összes Status és a NightMode.

## Források

- [Loxone: virtuális be- és kimenetek](https://www.loxone.com/enen/kb/virtual-inputs-outputs/)
- [FlexumShare gyártói integráció a Loxone Libraryben, 7. oldal](https://api.library.loxone.com/downloader/file/2733/Instruction%20Live-Data_FlexumShare%20platform_V1.1.pdf) – POST és `<v>` JSON body mintabeállítás; a jelen projektben az ott szereplő API-kulcs nem használandó.
