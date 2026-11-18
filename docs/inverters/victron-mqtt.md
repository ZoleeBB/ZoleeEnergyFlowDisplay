# Victron MultiPlus / Quattro és GX – helyi MQTT

A helyi MQTT adatforrás egy Cerbo GX vagy más, Venus OS-t futtató GX eszköz rendszerösszesítését használja. A MultiPlus/Quattro és a Solar Charger, illetve az AC-oldali PV-inverter a GX rendszerében legyen látható. A profilhoz **Venus OS 3.50 vagy újabb** kell. Ebben a verzióban a GX a kijelző saját kérésazonosítójával igazolja vissza, hogy az induló adatsor teljes. Régebbi GX-en a helyi Modbus TCP-profil használható.

1. A GX helyi szolgáltatásai között engedélyezd az **MQTT on LAN** szolgáltatást. A hozzáféréshez a GX hálózati és jogosultsági beállításai érvényesek.
2. A kijelző **Inverter beállítások** fülén válaszd a **Victron GX – MQTT** profilt. Írd be a GX helyi nevét vagy IP-címét és a **VRM Portal ID** 12 hexadecimális karakterét. Ez az azonosító a GX VRM-beállításaiban olvasható.
3. A 1883-as port a helyi titkosítatlan MQTT. A 8883-as port TLS-t használ. Saját hitelesítésszolgáltató esetén add meg a megbízható PEM-tanúsítványt és a tanúsítványban szereplő kiszolgálónevet. Ha a helyi broker bejelentkezést kér, a hozzá tartozó felhasználót és jelszót használd.
4. Telepített akkumulátor esetén jelöld a **Van akkumulátor** mezőt. A GX-ben kiválasztott akkumulátormérő vagy BMS adja a rendszer SOC-ját és teljesítményét. Hőmérséklet csak érvényes mérésnél jelenik meg.

| Kijelzett adat | GX-adat és számítás |
| --- | --- |
| PV-termelés | A DC napelem-szabályozók összesített teljesítménye és az AC-bemeneten, generátoroldalon, illetve AC-kimeneten mért PV összege. A kijelző ugyanazt a mérést egyszer számolja el. |
| Hálózat | Az `Ac/Grid` rendszerösszeg, illetve az összes jelzett fázis összege. Pozitív a vételezés, negatív a betáplálás. |
| Fogyasztás | `Ac/Consumption` a GX által meghatározott fogyasztásmérési körben. Részleges fázisjelből nem készül teljes házfogyasztás. |
| Akkumulátor | `Dc/Battery/Power`, `Soc` és `Temperature`. A GX pozitív töltőteljesítményét a kijelző negatív töltésként mutatja. |

A kijelző a `N/<Portal ID>/system/0/…` értesítésekre iratkozik fel. Az első teljes pillanatkép után öt másodpercenként az aktuális GX-munkamenet válaszával ellenőrzi az adatok elérhetőségét. Az értékváltozások közben is beérkeznek. A kapcsolat megszakadásakor a helyi gyorsítótár érvényét veszti. A következő kapcsolat felépítésekor a kijelző új teljes pillanatképre vár. Üres és null érvénytelenítést is kezel. Az akkumulátor nélküli profil a Battery topicokra nem iratkozik fel. A kimenő üzenetek `R/` olvasáskérések.

Ha nincs adat, ellenőrizd a helyi MQTT engedélyezését, a GX verzióját, a portot, a VRM Portal ID-t és TLS-kapcsolatnál a tanúsítvány nevét. A hiányzó hálózati vagy házmérés előtt nézd meg a GX saját rendszerábráján, hogy az adott mérő valóban megjelenik-e, és a fázisok mindegyike elérhető-e.

[Gyártói MQTT-protokoll és Venus-kompatibilitás](https://github.com/victronenergy/dbus-flashmq), [GX rendszerösszesítések](https://github.com/victronenergy/dbus-systemcalc-py), [Cerbo GX konfigurációs kézikönyv](https://www.victronenergy.com/media/pg/Cerbo_GX/en/configuration.html).
