# Grafikai assetek

A végleges dashboard nem LVGL szimbólumokkal vagy kézzel rajzolt primitív ikonokkal készül. A jóváhagyott látványterv vizuális karakteréhez külön SVG assetkészlet került a firmware-be.

A források a `main/app_assets.c` fájlban találhatók, 64×64-es vektorgrafikaként. Az LVGL 9.5 beépített SVG dekódere és ThorVG szoftveres vektor-renderere rajzolja őket.

A készlet tartalmazza többek között:

- ház + levél fejlécikon
- termelés levél
- fogyasztás ház
- hálózati oszlop és leválasztott hálózat
- érme / pénzügyi megtakarítás
- CO2 felhő
- fa
- önellátás villanykörte
- akkumulátor
- napelem
- részletes ház
- központi villám/hub
- napos, felhős, részben felhős, esős, havas és éjszakai időjárás
- szél
- mind a négy rendszerállapot
- Wi-Fi, MQTT és adat-timeout hibaállapotok

A kör alakú háttér, kártyakeret és az animált energiaáramlási pontok továbbra is LVGL layout/primitív elemek, mert ezek nem ikonok és dinamikusan változnak. A felismerhető piktogramok viszont mindenhol grafikai assetek.
