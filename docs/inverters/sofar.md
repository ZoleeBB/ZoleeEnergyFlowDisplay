# SOFAR HYD háromfázisú inverterek

A Közvetlen inverter mód a két PV-bemenetes **HYD 5KTL-3PH, HYD 6KTL-3PH, HYD 8KTL-3PH, HYD 10KTL-3PH, HYD 15KTL-3PH és HYD 20KTL-3PH** típusok G3 regisztertérképét olvassa. Telepített akkumulátor nélkül is kijelezhető a termelés. A profil kiválasztásához az inverter adattábláját és a kommunikációs modul jelölését használd. Az egyfázisú HYD-ES, az ESI, a KTL-X és a PV nélküli SP1ES120N6 tárolós változat kívül esik ezen a modellkörön.

## Kapcsolat beállítása

| Az inverter hálózati kapcsolata | Az Inverter beállítások lapon |
| --- | --- |
| Az inverter monitoring RS485-csatlakozójára kötött Modbus TCP–RTU átjáró | Válaszd a **SOFAR HYD … · Modbus TCP átjáró** profilt. Az átjáró a hálózati Modbus TCP-kérést soros Modbus RTU-kéréssé alakítsa. A TCP-port általában 502. A soros beállítások egyezzenek az inverterével. A G3 protokoll szerinti alapbeállítás 9600 bit/s, paritás nélkül, egy stopbittel. |
| Az inverteren helyi V5-kommunikációt biztosító SOLARMAN WiFi- vagy Ethernet-adatgyűjtő | Válaszd a **SOFAR HYD … · SOLARMAN V5** profilt. Az adatgyűjtőnek a kijelző helyi hálózatán kell elérhetőnek lennie. A szokásos TCP-port 8899. A decimális adatgyűjtő-sorozatszám a stick címkéjén található. Az inverter alfanumerikus sorozatszáma külön azonosító. |

Az **egységcím** mezőbe az inverteren beállított monitoring Modbus-cím kerüljön. A kijelző induló értéke 1. A SOFAR kézikönyve az RS485-lánc minden inverteréhez önálló, 1–31 közötti címet ír elő. Egy átjáró mögött így azonos IP-címen több inverter is elérhető a saját egységcímével. A beépített mérő és a BMS címe más célt szolgál.

A helyi keresést a kiválasztott profilhoz indítsd el. Ha csak az adatgyűjtőt találja meg, ellenőrizd az inverter egységcímét és a modul V5-kompatibilitását. A SOFAR alkalmazásban megjelenő felhős mérés önmagában nem jelzi a stick helyi V5-képességét. Kézi beállításkor add meg az átjáró vagy a stick helyi IP-címét. A mentés és a kijelzők időprogramjainak leírása a [Közvetlen inverter útmutatóban](../direct-inverter.md) található.

## Kijelzett mérések

Az olvasás célüteme 5 másodperc. A firmware kizárólag a 03-as Modbus-funkciót használja. A táblázatban szereplő hexadecimális számok nullától számozott PDU-címek. A regiszterszerkesztőben ellenőrizhetők az adott mérés alapértékei és visszaállítható minden felülírás.

| Mérés | Forrás és értelmezés |
| --- | --- |
| PV-termelés | 0x0586 és 0x0589, a két MPPT teljesítményének összege, 10 W felbontással. Ha valamelyik érték hiányzik, a firmware a 0x05C4 rendszerösszegét próbálja olvasni, 100 W felbontással. |
| Hálózat és házfogyasztás | A 0x0488 a csatlakozási pont előjeles teljesítménye, a 0x04AF a teljes rendszerterhelés. Mindkettő felbontása 10 W. A hálózati összehasonlításhoz működő, helyesen felszerelt mérés szükséges. A kijelző a vételezést pozitív előjellel kezeli. ID105 mérőkommunikációs hibánál (0x040B, 8. bit) mindkét érték kimarad. |
| Üzemállapot | A 0x0404 hármas értéke szigetüzemet, a négyes és ötös hibaállapotot jelez. |
| Akkumulátor | A **Van akkumulátor** jelölő kapcsolja be az olvasást. Elsődleges adat a 0x0667 teljes töltési teljesítmény (100 W) és a 0x0668 átlagos SOC (%). A kijelzőn a kisütés pozitív. |
| Régebbi akkuregiszterek | Az első és a második kör feszültsége (0x0604, 0x060B) mutatja a mért aktív köröket. Hiányzó összesített értéknél a kijelző a 0x0606/0x060D teljesítményeit összegzi (10 W), a 0x0608/0x060F töltöttségeit átlagolja. Ehhez mindkét kör jelenléte vagy hiánya, illetve a 0x066A szerinti aktív körszám legyen azonosítható. |
| Opcionális akkumulátor-hőmérséklet | A 0x0607 és 0x060E mezők közül az aktív akkukörök legmagasabb elérhető értéke jelenik meg. A 0 °C érvényes mérés. Hiányzó hőfok mellett a többi mérés használható. |

A profilokat a tényleges firmware-meghajtóval, szintetikus Modbus TCP- és SOLARMAN V5-válaszokkal ellenőriztük. Fizikai SOFAR inverteren végzett helyszíni teszt még szükséges. Az első beüzemeléskor hasonlítsd össze a termelést, a hálózati irányt és az akku töltési irányát az inverter saját energiaábrájával.

## Források

- [SOFAR HYD 5–20KTL-3PH gyártói kézikönyv, 5.11. fejezet és műszaki adatok](https://www.sofarsolar.com/upload/file/20231102/1698903560944035256.pdf)
- [SOFAR G3 háromfázisú Modbus regiszterjegyzék, v1.22, XLSX](https://github.com/user-attachments/files/18220633/SOFAR_Modbus_Protocol_English_G3_V1.22.only.3PH.xlsx)
- [SOFAR HYD 5–20KTL-3PH – SOLARMAN helyi profil és regiszterleképezés](https://github.com/StephanJoubert/home_assistant_solarman/blob/main/custom_components/solarman/inverter_definitions/sofar_g3hyd.yaml)
- [SOFAR sorozatszám-prefixek és modellváltozatok](https://github.com/darkrain-nl/sofar-modbus/blob/7e9f8f2/src/sofar_modbus/device.py), [SOLARMAN V5 keretezés](https://pysolarmanv5.readthedocs.io/en/latest/solarmanv5_protocol.html)
