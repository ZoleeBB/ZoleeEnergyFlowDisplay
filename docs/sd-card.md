# microSD előkészítés

A microSD tartalmazza a konfigurációt és a napi statisztikát.

## Fájlrendszer

A jelenlegi firmware-verzióhoz **FAT32** használata szükséges.

Az ESP-IDF 5.5.4 beépített FatFs rétegével az exFAT nincs bekapcsolva ebben a projektben, ezért exFAT-ra formázott kártyát jelenleg ne használj.

## Könyvtárstruktúra

Első indítás előtt csak ez szükséges:

```text
/config.json
```

A firmware automatikusan létrehozza:

```text
/stats/
```

és a napi fájlokat:

```text
/stats/2026-10-04.json
/stats/2026-10-03.json
```

A rendszer a mai és a tegnapi napi állományt tartja meg.

## config.json

1. Másold le a repository `config.sample.json` fájlját.
2. Nevezd át `config.json`-ra.
3. Töltsd ki legalább a Wi-Fi SSID-t és az MQTT broker címét.
4. Másold a microSD gyökerébe.

A valódi `config.json` szándékosan nincs Gitben.

## Írásbiztonság

A napi statisztika RAM-ban gyűlik, és alapértelmezés szerint óránként kerül SD-re.

Mentéskor a firmware ideiglenes fájlt és biztonsági cserét használ. Egy váratlan áramkimaradás után induláskor megpróbálja visszaállítani az utolsó érvényes napi állományt.

## Futás közbeni SD hiba

Ha az SD-kártya futás közben hibássá vagy elérhetetlenné válik:

- az MQTT fogadás folytatódik,
- a dashboard működik tovább RAM-ból,
- piros SD-kártya hiba jelenik meg,
- a firmware 1 percenként megpróbálja helyreállítani a kapcsolatot.
