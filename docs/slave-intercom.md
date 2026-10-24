# Kaputelefon a Master és 4B Slave kijelzőn

A párosított Waveshare ESP32-P4-WIFI6-Touch-LCD-4B kijelző a Masteren mentett kaputelefont használja. Külön Slave-beállítófelület és SD-kártya nem szükséges. A Wi-Fi profil továbbra is a Master meglévő, PIN-nel védett szinkronjából származik. Mindkét firmware frissítése szükséges; régi Master mellett a Slave energia- és jelenlétfunkciói változatlanul működnek, kaputelefon nem indul.

A csengetés minden csatlakoztatott kijelzőn megnyitja a kameraképet. A Slave 720×720-as felületén a kép aránytartóan legfeljebb 720×460 pixel, alatta érinthető beszélgetés- és kapunyitógomb jelenik meg. A hang és a kamera közvetlen Wi-Fi-kapcsolat a kaputelefonhoz. A Miniserverhez továbbra is csak a Master csatlakozik; minden Slave-kapunyitás azon keresztül megy.

## Egyetlen hanghívás

A Master a saját gombját is ugyanazzal a központi foglalással kezeli. Az engedély a SIP INVITE és a mikrofon megnyitása előtt jön létre. Másik kijelzőn a beszélgetésgomb inaktív, a kép és a kapunyitás elérhető marad. A tulajdonos a hívásindítást is megszakíthatja. Bontáskor az engedély csak a kodek és a socketek bezárása után szabadul fel.

A 1007 BLE karakterisztika 20 bájtos vezérlést és állapotot ad. A 1008 (512 bájt) és 1009 (256 bájt) két verziózott profilt visz át; ezek fejléceinek és a státusznak ugyanahhoz a Master-boothoz és konfiguráció-/csengetésgenerációhoz kell tartozniuk. Az opcionális karakterisztikák nem változtatják meg a régi energia-, óra-, Wi-Fi- és statisztikai formátumot. Minden intercom karakterisztika LE Secure Connections, MITM, bond, 16 bájtos titkosítási kulcs és regisztrált peer ellenőrzést kér.

A Slave 3,5 másodpercnél régebbi visszaigazolással nem tarthat nyitva mikrofont. A Master 15 másodpercig tartja a távoli foglalást; újraindulás után ugyanennyi ideig nem enged új hanghívást. A hálózat felépítési/bontási határideje ezen az időkereten belül marad. Kapunyitás minden kijelzőn leállítja a csengőhangot, az aktív hanghívás 5 másodperc után bontódik. Lejárt vagy más boothoz, generációhoz, tulajdonoshoz vagy engedélyazonosítóhoz tartozó parancs nem indíthat hívást. Újracsatlakozás nem örökli a régi engedélyt.

A kamera adminjelszava csak a védett BLE profilon jut át, a Slave RAM-jában él; sem a publikus webes válaszba, sem a Slave NVS-ébe nem kerül. A Master eszközfüggően titkosított SD-fájlját nem másoljuk. A helyi kamera HTTP/MJPEG és SIP/RTP forgalma az eddigi integrációnak megfelelően a helyi hálózaton fut.

## Hangillesztés

Master: 16 kHz, kétcsatornás STD I2S. 4B Slave: a rögzített 3.0.1 BSP 24 kHz-es STD TX / négy fizikai slotos TDM RX útvonala. FL mikrofon = slot0; a codec egycsatornás PCM-ként adja át. A beszélgetés mindkét kijelzőn 8 kHz-es G.711 és ESP-SR VoIP AEC, a Slave három 24 kHz-es időminta átlagából állítja elő a 8 kHz-es mikrofonjelet. A csengőhang nem nyit mikrofont, SIP-et vagy AEC-t. A Slave a kis I2S/DMA-területet már induláskor, a Wi-Fi/BLE és a videó előtt lefoglalja; a hangkodek és a mikrofon ettől még zárva marad.

## Csengőhangcsomag

A Master Loxone beállításaiban ZIP tölthető fel, a készülék a `/sdcard/ringtones` könyvtárba bontja. Legfeljebb 8 MiB ZIP, legfeljebb 32 hang. A ZIP gyökerébe kerüljenek a fájlok, alkönyvtár nélkül. Fájlnév: kisbetű, szám, kötőjel vagy aláhúzás és `.wav` végződés, legfeljebb 64 karakter. Opcionális `ringtone.json` a források és licencek leírásához. A csomag a korábbi hangkészletet cseréli; hibás vagy megszakadt telepítés a korábbi teljes készletet hagyja meg. A mentett csengőhang-választás ettől külön beállítás.

A közös lejátszási formátum **WAV, tömörítetlen PCM signed 16 bit little-endian, mono, 16000 Hz**, legfeljebb **8 másodperc**. MP3, float WAV, sztereó vagy más mintavételű fájl feltöltése hibát ad. Példa átalakítás számítógépen:

```sh
ffmpeg -i sajat-hang.mp3 -ac 1 -ar 16000 -c:a pcm_s16le -t 8 csengo.wav
```

A legördülőben a telepített hangok választhatók; alapból „Nincs csengőhang”, ismétlés kikapcsolva. Ismétlés nélkül a hang egyszer indul a csengetés elején. Ismétléssel a csengetés végéig szól, de egy beszélgetés engedélyezése vagy elfogadott kapunyitás az adott csengetés hátralévő idejére minden kijelzőn elnémítja. A következő csengetés újra megszólalhat. A hangerő a mentett kaputelefon-hangerőt követi.

A Slave a kiválasztott WAV-ot a Mastertől tölti le egy BLE-n kapott, véletlen, Master-bootonként cserélődő médiakulccsal; a lejátszás RAM-ból történik. Letöltési/SD-hiba vagy hiányzó hang nem akadályozza a kamerát és a beszélgetést.

A kezdőcsomag három Freesound CC0 hangot tartalmaz: Squidems „Doorbell”, marlocarlo „Doorbell”, SoundProject123456789 „Doorbell”. A pontos URL-ek, licencek, eredeti fájl SHA-256 értékei és átalakítások a csomag `ringtone.json` fájljában szerepelnek. Kiadáskor a hangok már a forrásban vannak, nincs hálózati letöltés.

A release két közös melléklete: `ZoleeEnergyFlowDisplay-<verzió>-templates.zip` (a témák és a Loxone Otthon külön telepíthető ZIP-jei) és `ZoleeEnergyFlowDisplay-<verzió>-ringtones.zip` (közvetlenül feltölthető csengőhangcsomag). Mindkettő SHA-256 ellenőrzőösszeget kap. A Master USB-csomag SD-kezdőkészlete is tartalmazza a hangokat.

## Fizikai átvétel

- Master és legalább két Slave: egyidejű csengetés, aránytartó videó, nappali/éjszakai fényerő és visszatérés.
- Egyidejű beszélgetésgomb: egyetlen SIP-hívás és mikrofon; a többi gomb tiltott. Bontás és sikertelen hívás után újraindítható.
- Master/Slave BLE-kiesés és újraindulás hívás közben: helyi hang leáll, lejárt engedély nem éleszthető fel, csak a biztonsági idő letelte után kezdhető másik hívás.
- Kapunyitás bármely kijelzőről: egyetlen impulzus, csengőhang leáll, aktív beszélgetés 5 s után bont.
- Egyszeri és ismétlődő csengőhang mindhárom hanggal; váltás beszélgetésre, ring 0/1, hiányzó WAV, megszakadt csomagfeltöltés.
- 4B csatlakoztatott 8 Ω / 2 W hangszóró, FL mikrofon, hangerő/erősítés, akusztikus visszhang és videóval egyidejű duplex terhelés.
- A kaputelefon több párhuzamos MJPEG kliensének tényleges támogatása kameramodellfüggő. Ha a kamera korlátozza a streameket, külön videórelé szükséges; a jelenlegi integráció közvetlen kapcsolatokat nyit.
