# Konfiguráció

A firmware a konfigurációt kizárólag induláskor olvassa be a microSD gyökeréből:

`/config.json`

Ha a fájl hiányzik, nem olvasható vagy egy megadott érték érvénytelen, a normál működés nem indul el. Az opcionális mezők hiánya nem hiba: ilyenkor a firmware az alapértéket használja. Ismeretlen extra mezők figyelmen kívül maradnak.

## project

- `title`: kijelző fejlécének neve. Alapérték: `Otthoni energia`
- `language`: `hu` vagy `en`, kis-/nagybetűtől függetlenül
- `currency`: egyszerű megjelenített pénznemszöveg. Alapérték: `Ft`

## wifi

- `ssid`: kötelező
- `password`: üres lehet
- `reconnect_seconds`: alapérték 15
- `network.use_static_ip`: 0 = DHCP, 1 = statikus IP
- statikus IP esetén az `ip`, `gateway`, `subnet`, `dns` mezők kötelezőek

## time

- `ntp_server`: `hu.pool.ntp.org`
- `timezone`: `Europe/Budapest`
- `auto_dst`: 1
- `utc_offset_minutes`: 60; csak kikapcsolt automatikus DST mellett használt
- `startup_retry_seconds`: 15
- `resync_hours`: 6
- `background_retry_minutes`: 15

## mqtt

- `broker`: kötelező hostname vagy IP
- `port`: 1883
- `topic`: `loxberry/mqttgateway/energymonitor/state`
- `username`, `password`: opcionális
- `data_timeout_seconds`: 30
- `reconnect_seconds`: 15

Az MQTT QoS fixen 1.

## display

- `day_brightness`: 80
- `night_brightness`: 20

## statistics

- `save_interval_minutes`: 60
- `max_integration_gap_seconds`: 30

A grafikon mintaintervalluma fixen 5 perc.

## energy

- `electricity_purchase_price`: 65
- `feed_in_price`: 5
- `co2_factor_kg_per_kwh`: 0.381
- `tree_absorption_kg_per_year`: 22

## ota

- `username`
- `password`

Ha mindkettő üres, a helyi webes információs és OTA oldal hitelesítés nélkül érhető el. Ha használunk hitelesítést, mindkét mezőt ki kell tölteni.
