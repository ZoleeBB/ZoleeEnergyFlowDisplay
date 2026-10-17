# microSD

FAT32 fájlrendszer szükséges; exFAT nem támogatott. A gyökérben legyen `config.json`, a kiválasztott [konfigurációs minta](configuration.md) alapján.

A kilenc választható nézet könyvtárai a `/templates/<azonosító>/` alatt legyenek. Minden csomag saját `template.json`-t és az abban hivatkozott PNG-ket, valamint `preview-light.png` és `preview-dark.png` képet tartalmaz. A régebbi, előnézet nélküli csomag továbbra is betölthető; a webes keret csak a frissítés után kap képet. A kiválasztás `selection.*` fájljait a készülék kezeli. Másoláskor a csomag JSON-ját és képeit együtt frissítsd; [telepítési útmutató](templates.md).

A statisztikát a firmware maga hozza létre és 35 helyi naptári napig őrzi. Egy nap 288 ötperces időablakot tartalmaz. A `.tmp` és `.bak` fájlok helyreállítási állományok: működés közben ne töröld őket. Induláskor a legújabb érvényes revízió kerül betöltésre. [Adatformátum és számítás](statistics.md).

Induláskor az SD és a konfiguráció szükséges. Futás közben SD-hibánál a mérés és a RAM-beli statisztika tovább működik; a hiba a diagnosztikában látható. Újracsatlakoztatáskor a firmware ismét próbál menteni. Kártyacsere és kézi fájlmódosítás előtt kapcsold ki a készüléket.

A közvetlen Loxone mód `loxone.json` konfigurációt is tárol. A jelszó eszközhöz kötött; másik kijelzőn újra megadandó. A diagnosztika az SD foglalt/teljes méretét és százalékos telítettségi sávját mutatja. A kapacitás legfeljebb percenként frissül; induláskor rövid ideig még nem áll rendelkezésre.
