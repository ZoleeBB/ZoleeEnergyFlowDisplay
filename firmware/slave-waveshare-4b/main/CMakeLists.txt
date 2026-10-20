set(shared "${CMAKE_CURRENT_LIST_DIR}/../../../main")
set(board "${CMAKE_CURRENT_LIST_DIR}/..")
set(shared_sources
    app_ble_seed.c app_ble_protocol.c app_ble_profile.c app_ble_profile_model.c
    app_ble_stats_protocol.c app_config.c app_boot_config.c app_events.c
    app_ingest.c app_payload.c app_state.c app_stats.c app_time.c app_wifi.c
    app_ota.c app_ota_auto.c app_ota_release.c app_display.c app_slave_display.c)
set(sources slave_4b_main.c slave_4b_storage.c "${board}/ui/slave_4b_ui.c"
    "${CMAKE_CURRENT_BINARY_DIR}/slave_4b_assets.c")
foreach(source IN LISTS shared_sources)
    list(APPEND sources "${shared}/${source}")
endforeach()
foreach(size 16 20 28)
    list(APPEND sources "${shared}/fonts/app_font_${size}.c")
endforeach()
idf_component_register(SRCS ${sources}
    INCLUDE_DIRS "${shared}" "${board}/ui"
    REQUIRES app_update bt esp_event esp_http_client esp_netif esp_timer esp_wifi
             fatfs sdmmc json mbedtls nvs_flash)
execute_process(COMMAND "${PYTHON}" "${board}/tools/assets.py"
    "${CMAKE_CURRENT_BINARY_DIR}/slave_4b_assets.c" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB slave_4b_assets "${board}/assets/*.png" "${shared}/assets/png/*.png")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${slave_4b_assets} "${board}/tools/assets.py")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    APP_OTA_ASSET_PREFIX="ZoleeEnergyFlowDisplay-slave-4b")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=misleading-indentation -Werror=frame-larger-than=8192)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_lv_adapter_register_display"
    "-Wl,-u,__wrap_esp_lv_adapter_register_display")
