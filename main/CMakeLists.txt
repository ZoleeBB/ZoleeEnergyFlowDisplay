# A teljes PNG készlet és a valódi fájlméretek egy forrásból származnak.
set(app_png_files
    "assets/png/flow_battery.png"
    "assets/png/flow_grid.png"
    "assets/png/flow_house.png"
    "assets/png/flow_hub.png"
    "assets/png/flow_solar.png"
    "assets/png/grid_off.png"
    "assets/png/header_home_leaf.png"
    "assets/png/metric_coins.png"
    "assets/png/metric_grid.png"
    "assets/png/metric_house.png"
    "assets/png/metric_leaf.png"
    "assets/png/mqtt_off.png"
    "assets/png/status_error.png"
    "assets/png/status_island.png"
    "assets/png/status_normal.png"
    "assets/png/status_prepare.png"
    "assets/png/summary_bulb.png"
    "assets/png/summary_co2.png"
    "assets/png/summary_tree.png"
    "assets/png/timeout.png"
    "assets/png/waiting_data.png"
    "assets/png/weather_cloud.png"
    "assets/png/weather_moon.png"
    "assets/png/weather_partly.png"
    "assets/png/weather_rain.png"
    "assets/png/weather_snow.png"
    "assets/png/weather_sun.png"
    "assets/png/wifi_off.png"
    "assets/png/wind.png"
)


# Az alkalmazás saját ESP-IDF komponense.
idf_component_register(
    SRCS
        "app_main.c"
        "app_assets.c"
        "app_chart.c"
        "fonts/app_font_12.c"
        "fonts/app_font_14.c"
        "fonts/app_font_16.c"
        "fonts/app_font_18.c"
        "fonts/app_font_20.c"
        "fonts/app_font_22.c"
        "fonts/app_font_28.c"
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
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    EMBED_FILES ${app_png_files}
    REQUIRES
        app_update
        esp_event
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        fatfs
        sdmmc
        json
        mbedtls
        mqtt
        nvs_flash
)

# Az ESP-IDF korai függőségvizsgálata az idf_component_register() hívásnál
# visszatér. Fájlműveleteket csak a rendes konfigurálási fázisban végzünk.
set(asset_sizes_header "${CMAKE_CURRENT_BINARY_DIR}/app_asset_sizes.h")
file(WRITE "${asset_sizes_header}" "/* Generated from PNG file sizes. */\n#pragma once\n")
foreach(png IN LISTS app_png_files)
    set(png_path "${CMAKE_CURRENT_LIST_DIR}/${png}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${png_path}")
    file(SIZE "${png_path}" png_size)
    get_filename_component(png_name "${png}" NAME_WE)
    string(TOUPPER "${png_name}" png_name)
    file(APPEND "${asset_sizes_header}" "#define APP_PNG_${png_name}_SIZE ${png_size}U\n")
endforeach()

# A figyelmeztetéseket megtartjuk, de a régi, egysoros kódrészek formázási
# figyelmeztetései egyelőre ne állítsák meg a buildet.
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
    -Wno-error=misleading-indentation
    -Werror=frame-larger-than=8192
)


