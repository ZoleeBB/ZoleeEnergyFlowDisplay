# Működési áttekintés

## Indítási sorrend

1. kijelző/LVGL inicializálás
2. SD-kártya csatlakoztatás
3. `config.json` betöltés és ellenőrzés
4. Wi-Fi kapcsolat
5. NTP időszinkron
6. statisztika betöltés
7. helyi web/OTA szolgáltatás
8. MQTT kapcsolat
9. első érvényes MQTT állapotcsomag
10. dashboard

Az SD vagy konfiguráció induláskori hibája blokkoló hiba. Futás közbeni SD-hiba nem állítja le az MQTT megfigyelést és a kijelzőt.

## Adatfeldolgozás

Az MQTT payload minden mezője kötelező. A csomag csak akkor frissíti a kijelzőt és a statisztikát, ha minden mező típusa és tartománya megfelelő.

A pillanatnyi mintákból folyamatos energia-integrálás történik. A 30 másodpercnél nagyobb minta-kimaradást alapértelmezés szerint nem extrapoláljuk.

A grafikonok 5 perces átlagpontokból állnak, napi 288 ponttal.

## OTA rollback

Az új OTA firmware legfeljebb 3 percet kap arra, hogy sikeresen elérje az SD-kártyát, betöltse a konfigurációt, csatlakozzon Wi-Fihez, időt szinkronizáljon és kapcsolódjon az MQTT brokerhez. Érvényes MQTT adatcsomag érkezése nem feltétele az OTA firmware elfogadásának.
