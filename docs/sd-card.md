# microSD

FAT32 fájlrendszer szükséges; exFAT nem támogatott. A gyökérben legyen `config.json`, a kiválasztott [konfigurációs minta](configuration.md) alapján.

A nyolc választható nézet könyvtárai a `/templates/<azonosító>/` alatt legyenek. Minden csomag saját `template.json`-t és az abban hivatkozott PNG-ket tartalmazza. A kiválasztás `selection.*` fájljait a készülék kezeli. Másoláskor a csomag JSON-ját és képeit együtt frissítsd; [telepítési útmutató](templates.md).

A statisztikát a firmware maga hozza létre és 35 helyi naptári napig őrzi. Egy nap 288 ötperces időablakot tartalmaz. A `.tmp` és `.bak` fájlok helyreállítási állományok: működés közben ne töröld őket. Induláskor a legújabb érvényes revízió kerül betöltésre. [Adatformátum és számítás](statistics.md).

Induláskor az SD és a konfiguráció szükséges. Futás közben SD-hibánál a mérés és a RAM-beli statisztika tovább működik; a hiba a diagnosztikában látható. Újracsatlakoztatáskor a firmware ismét próbál menteni. Kártyacsere és kézi fájlmódosítás előtt kapcsold ki a készüléket.
