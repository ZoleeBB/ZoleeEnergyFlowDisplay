# Közvetlen Loxone HTTP POST adatküldés

**Állapot:** a HTTP POST fogadó megvalósítva. A minta célkörnyezete Miniserver Gen 2, Loxone Config és firmware 17.x; a tényleges Miniserveren végzett próba még szükséges.

## Adatút

A Program blokk I1–I13 numerikus és opcionális T1 szöveges bemenetéből a Pico C program öt másodpercenként teljes JSON body-t készít. A Txt1 kimenetet a HTTP virtuális kimeneti parancshoz kell kötni. A Miniserver közvetlenül a kijelzőnek küldi a POST-kérést; MQTT broker és közvetítő nem szükséges.

Minta: [loxone-energy-http-post.picoc](../examples/loxone-energy-http-post.picoc).

## Kijelző konfigurációja

Teljes, broker nélküli minta: [config.http.sample.json](../examples/config.http.sample.json).

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

A bemenetek száma összesen **14**, de ez `I1`–`I13` és `T1`, nem `I1`–`I14`. A legfelső T1 szövegbemenet adja a `SunPosition` karakterláncot.


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
| I10 | Status | 1: normál; 2: felkészülés; 3: szigetüzem; 4: hiba; 5: hálózat újra elérhető |
| I11 | NightMode | 0/1 |
| I12 | BatteryTemperature | °C |
| I13 | WeatherPresent | 0–15, időjárási jelenlétmaszk |
| **14. bemenet: T1** | **SunPosition** | `azimut#magasság#napkeltePerc#napnyugtaPerc`, opcionális |


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

## Időjárás jelenlétjelzés (I13)

Az **I1–I12 sorrendje változatlan**. A minták az eddig szabad **I13** bemenetet
használják `WeatherPresent` jelenlétmaszkként; ez nem lesz új JSON-mező.
A dokumentált Pico C API-ban nincs bekötöttség-lekérdezés: a `getinput()`
értéket ad vissza, a `getinputevent()` változást jelez. Emiatt a bekötetlen
bemenetet nem lehet megbízhatóan megkülönböztetni egy valódi nulla értéktől.
A jelenlétet ezért a Loxone Configban kell megadni, vagy adatérvényességi jelekből
előállítani. **A script nem ismeri fel önállóan a vezeték csatlakoztatását.**

| Bekötött és érvényes adat | A maszkhoz hozzáadandó érték |
| --- | ---: |
| I6: Temperature | 1 |
| I7: WindSpeed | 2 |
| I8: HasSunRadiation | 4 |
| I9: HasPrecipitation | 8 |

Minden időjárásbemenet használatakor I13 = **15**. Ha például csak a hőmérséklet
és a szél van bekötve, I13 = **3**. Időjárás nélkül I13 = **0** (bekötetlen
I13 esetén is ez az alaphelyzet). A nem használt bemenet bitjét 0-ra kell
állítani: ekkor annak JSON-értéke `null`, miközben a kulcs megmarad. Egy
jel leválasztásakor a maszkot is módosítani kell. Ha érzékelőadat kimarad,
a maszk megfelelő bitjét annak adatérvényességi jelével kell kikapcsolni.
Negatív, 15 feletti vagy tört maszk biztonságosan 0-ra esik vissza.
A valódi 0 °C, 0 km/h és 0 állapotjel bekötött/jelenlévő bit mellett megmarad.

Hivatalos API: [Loxone Custom Script Programming](https://www.loxone.com/dede/wp-content/uploads/sites/2/2016/08/CustomScriptProgramming.pdf).
A megjelenítési szabályok: [MQTT adatprotokoll](mqtt.md#opcionális-időjárási-értékek).

Kapcsolódó útmutató: [dokumentáció és első indítás](../README.md).

## Opcionális napadatok HTTP módban

A T1-re csatlakoztatott összefűzött szöveg formátuma az MQTT-módéval **azonos**.
Például `327.445#-46.562#420#1077`. A Pico C minta ezt a JSON
`SunPosition` mezőjébe illeszti, üres bemenetre `null`-t ír.
A webes / HTTP adatfogadó és az MQTT ugyanazt az ellenőrző rutint használja.
A teljes bemenetkiosztást a [Loxone útmutató](loxone.md) is tartalmazza.
