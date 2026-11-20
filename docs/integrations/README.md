# Adatkapcsolatok és Master/Slave

[Dokumentációs kezdőlap](../README.md) · [Projektáttekintés](../../README.md)

Energiaadatok fogadása MQTT-n, HTTP-n, közvetlenül Loxone-ról, inverterről vagy Bluetooth-on.

## Kapcsolat kiválasztása

MQTT és HTTP POST esetén a külső adatküldőt kell beállítani. Közvetlen Loxone esetén a Miniserver állapotait párosítsd. Közvetlen inverter módban a Masterhez egy aktív helyi vagy gyártói felhős kapcsolatot válassz. A Bluetooth Master/Slave és a 4B
firmware külön leírást kapott. Az alábbi protokoll- és konfigurációs
szerződések a felhasználói beállításhoz és a fejlesztéshez is elérhetők.

## Útmutatók

- [Közvetlen inverter – családok, helyi és felhős elérés, sötétítés](../direct-inverter.md)
- [JSON és MQTT](../mqtt.md)
- [HTTP adatfogadás](../http-input.md)
- [Közvetlen Loxone kapcsolat](../loxone-direct.md)
- [Modern Audioserver – natív kapcsolat, jogosultságok és diagnosztika](../audio-controller.md#kapcsolat-jogosultság-és-hibák)
- [Loxone mintaprogramok](../loxone.md)
- [BLE master/Slave, PIN-es párosítás és SD nélküli működés](../ble-data-seed.md)
- [Master jelenlét szerinti nappali/éjszakai sötétítése](../loxone-direct.md#mester-jelenlét-szerinti-sötétítése)
- [Slave-jelenlétprofil, nappali/éjszakai sötétítés és BLE-protokoll](../slave-presence-protocol.md)
- [Waveshare 4B Slave – 720×720-as firmware és szimulátor](../slave-waveshare-4b.md) (külön alkalmazás, falra vagy asztali tartóra)
- [Loxone fogyasztók és lapozható Otthon-kártyák – beállítás és mintaprofil](../v3-home-config.md)
- [Waveshare 4B Slave – firmware fordítása, működése és hardveres átvétele](../../firmware/slave-waveshare-4b/README.md)
