# Funkciók és vezérlés

[Dokumentációs kezdőlap](../README.md) · [Projektáttekintés](../../README.md)

Energiastatisztika, Loxone Otthon, világítás, Audio, kaputelefon és kiegészítő funkciók.

## Mit szeretnél használni?

A világításvezérlő részletes beállításai és mind az öt előnézeti képe a
[világítási útmutatóban](../lighting-controller.md#statikus-előnézetek)
található. A fő README csak a funkciót sorolja fel.

## Otthon-kártyák leírásai és képei

A kártyák a **10.1-es Master közvetlen Loxone módjában** használhatók.
A 4B Slave és a Master MQTT-, HTTP- vagy Slave-módja nem kapja meg ezeket
a Loxone-vezérlőket. A táblázat képei a tényleges LVGL-felületből,
megjelölt tesztadatokkal készültek; a beállítási és működési korlátok a
hivatkozott útmutatókban szerepelnek.

| Kártya | Loxone-blokk | Leírás | Kép |
| --- | --- | --- | --- |
| Világításvezérlő | `LightControllerV2` | [Világítás és részletes kezelők](../lighting-controller.md) | [Teljes képgaléria](../lighting-controller.md#statikus-előnézetek) |
| IRC termosztát | `IRoomControllerV2` | [Hőfokállítás és üzemmódjelzés](../irc-controller.md) | [Nappali kártyák](../images/irc/irc-cards-day.jpg) |
| Automatikus és központi árnyékolás | `Jalousie`, `CentralJalousie` | [Redőnyök és százalékos állítás](../shading-controller.md) | [Kártyák](../images/shading/shading-cards-day.jpg), [részletes kezelő](../images/shading/shading-position-day.jpg) |
| Önálló dimmer | `Dimmer` | [Közvetlen fényerőállítás](../loxone-home-controls.md#dimmer-és-analóg-értékállító) | [Dimmer-kezelő](../images/controls/controls-dimmer-day.png) |
| Központi világítás | `CentralLightController` | [Csoportos Be/Ki](../loxone-home-controls.md#központi-világítás) | [Összes világítás kártya](../images/controls/controls-cards-day.png) |
| Analóg értékállító | `Slider` | [Célérték és Beállítás](../loxone-home-controls.md#slider) | [Slider-kezelő](../images/controls/controls-slider-day.png) |
| Részletes időzített kapcsoló | `TimedSwitch` | [Időzítés és Tartós be](../loxone-home-controls.md#részletes-időzített-kapcsoló) | [Időzítő-kezelő](../images/controls/controls-timer-day.png) |
| Ajtó–ablak összesítő | `WindowMonitor` | [Állapotok és lapozható lista](../loxone-home-controls.md#ajtóablak-összesítő) | [Összesítő-kezelő](../images/controls/controls-monitor-day.png) |
| Kapu / garázsajtó | `Gate` | [Irányok, tiltások és hat ikonváltozat](../loxone-home-controls.md#kapu-garázsajtó-és-motoros-ablak) | [Kerti kapu kártya](../images/controls/controls-cards-day.png) |
| Motoros ablak | `Window` | [Nyitottság és célpozíció](../loxone-home-controls.md#kapu-garázsajtó-és-motoros-ablak) | [Ablak-kezelő](../images/controls/controls-window-day.png) |
| Audio | `AudioZoneV2` | [Lejátszás, hangerő és kedvencek](../audio-controller.md) | [Kártyák](../images/audio/audio-cards-day.png), [hangerő](../images/audio/audio-volume-day.png), [részletes kezelő](../images/audio/audio-detail-night.png) |

Az általános érték-, állapot-, kapcsoló- és nyomógombkártyák, valamint az
oldalak fülei és a kártyánkénti fel/le rendezés az
[Otthon-konfiguráció](../v3-home-config.md) részei. Az IRC és a redőny képe
az új kártyákéval együtt az [Otthon v4 SD-csomagból](../v3-home-config.md#v4-otthon-erőforráscsomag-frissítése)
töltődik be; ezekhez külön firmware-be épített ikon már nem szükséges.

## Útmutatók

- [Részletes funkció- és üzemmód-áttekintés – a korábbi README kibontott leírása](../project-overview.md)
- [Statisztika és számítások](../statistics.md)
- [Akku diagnosztika – energia/SOC, kapacitásbecslés, hatásfok és webes statisztika/nullázás](../battery-diagnostics.md)
- [Helyi energiaasszisztens – napelemes indításjavaslat és éjszakai fogyasztás](../energy-assistant.md)
- [Világításvezérlés – kártyák, dimmerek, RGB/RGBW, teljes statikus képgaléria](../lighting-controller.md)
- [IRC termosztát – célhőfok, szobahőmérséklet és natív 0,5 °C-os léptetés](../irc-controller.md)
- [Redőnyvezérlés – automatikus és központi árnyékolás, Fel / Stop / Le és külön százalékos állítás](../shading-controller.md)
- [További Otthon-blokkok – dimmer, központi világítás, Slider, időzítő, ajtó–ablak összesítő, kapu és motoros ablak](../loxone-home-controls.md)
- [Modern Audioserver – borítókép, hangerő, részletes vezérlés és teljes kedvenclista](../audio-controller.md)
- [Kaputelefon – videó, kétirányú hang, hangerők és időzített kapunyitás utáni bontás](../intercom.md)
- [Kaputelefon utáni témavisszatöltés – időkorlátok, tartalék nézet és hibaelhárítás](../intercom.md#kaputelefon-utáni-témahelyreállítás)
- [4B Slave kaputelefon, központi hívásfoglalás és csengőhangcsomagok](../slave-intercom.md)
- [Energiaáramlás tiltás – Otthon, logós készenlét, 10% fényerő és Slave-ek](../energy-flow-disable.md)
- [Elektromos autó – töltőbeállítás, napi energia és napelemes arány](../electric-car.md)
- [Opcionális relatív páratartalom, témák és Slave-frissítési sorrend](../relative-humidity.md)
