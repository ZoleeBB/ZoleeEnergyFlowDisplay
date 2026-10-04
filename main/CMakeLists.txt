# Az alkalmazás saját ESP-IDF komponense.
idf_component_register(
    SRCS
        "app_main.c"
        "app_assets.c"
        "app_config.c"
        "app_events.c"
        "app_mqtt.c"
        "app_ota.c"
        "app_payload.c"
        "app_state.c"
        "app_stats.c"
        "app_storage.c"
        "app_time.c"
        "app_ui.c"
        "app_web.c"
        "app_wifi.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "assets/png/header_home_leaf.png"
        "assets/png/metric_leaf.png"
        "assets/png/metric_house.png"
        "assets/png/metric_grid.png"
        "assets/png/metric_coins.png"
        "assets/png/flow_battery.png"
        "assets/png/flow_grid.png"
        "assets/png/flow_house.png"
        "assets/png/flow_hub.png"
        "assets/png/flow_solar.png"
    REQUIRES
        app_update
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        fatfs
        json
        mbedtls
        mqtt
        nvs_flash
)

# A figyelmeztetéseket megtartjuk, de a régi, egysoros kódrészek formázási
# figyelmeztetései egyelőre ne állítsák meg a buildet.
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    -Wno-error=misleading-indentation
)
