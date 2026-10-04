idf_component_register(
    SRCS
        "app_main.c"
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

# Keep warnings visible, but do not fail the build on formatting-only
# misleading-indentation diagnostics while the source is being cleaned up.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=misleading-indentation)
