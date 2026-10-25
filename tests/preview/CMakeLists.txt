cmake_minimum_required(VERSION 3.16)
project(zolee_ui_preview C CXX)
set(CMAKE_C_STANDARD 11)
option(ZOLEE_PREVIEW_ASAN "Check preview memory accesses and leaks" OFF)
if(ZOLEE_PREVIEW_ASAN)
    # A teljes LVGL és a tényleges UI is műszerezett; a firmware-buildet nem érinti.
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ZOLEE_PREVIEW_ASAN requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -O1 -g1)
    add_link_options(-fsanitize=address)
endif()
get_filename_component(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
file(GLOB FONT_SOURCES "${APP_ROOT}/main/fonts/*.c")
file(GLOB PNG_FILES "${APP_ROOT}/main/assets/png/*.png")
file(WRITE "${CMAKE_BINARY_DIR}/app_asset_sizes.h" "#pragma once\n")
set(PNG_OBJECTS)
foreach(PNG IN LISTS PNG_FILES)
    get_filename_component(NAME "${PNG}" NAME)
    get_filename_component(STEM "${PNG}" NAME_WE)
    string(TOUPPER "${STEM}" UPPER)
    file(SIZE "${PNG}" BYTES)
    file(APPEND "${CMAKE_BINARY_DIR}/app_asset_sizes.h" "#define APP_PNG_${UPPER}_SIZE ${BYTES}U\n")
    set(OBJECT "${CMAKE_BINARY_DIR}/${NAME}.o")
    add_custom_command(OUTPUT "${OBJECT}" COMMAND ${CMAKE_LINKER} -r -b binary "${NAME}" -o "${OBJECT}"
        WORKING_DIRECTORY "${APP_ROOT}/main/assets/png" DEPENDS "${PNG}")
    list(APPEND PNG_OBJECTS "${OBJECT}")
endforeach()
add_executable(ui_preview preview.c "${APP_ROOT}/main/app_events.c" "${APP_ROOT}/main/app_diagnostics.c" "${APP_ROOT}/main/app_chart.c" "${APP_ROOT}/main/app_assets.c"
    slave_display_input_test.c "${APP_ROOT}/main/app_slave_display.c" "${APP_ROOT}/main/app_slave_display_input.c"
    "${APP_ROOT}/simulator/home_fixture.c" "${APP_ROOT}/simulator/battery_fixture.c" "${APP_ROOT}/main/app_battery_diagnostics.c" "${APP_ROOT}/main/app_energy_assistant.c" "${APP_ROOT}/main/app_templates.c" "${APP_ROOT}/main/app_template_zip.c" "${APP_ROOT}/main/app_ringtone_wav.c" ${FONT_SOURCES} ${PNG_OBJECTS})
target_include_directories(ui_preview PRIVATE "${APP_ROOT}/simulator/compat" "${APP_ROOT}/tests/stubs" "${APP_ROOT}/main" "${CMAKE_BINARY_DIR}")
target_compile_definitions(ui_preview PRIVATE time=host_time APP_TEMPLATE_DESKTOP)
target_compile_options(ui_preview PRIVATE -Wall -Wextra -Werror -Wno-misleading-indentation -Werror=frame-larger-than=8192)
if(ZOLEE_PREVIEW_ASAN)
    # A teszt main() ASan-védőterületei növelik a host stackkeretét; a
    # firmware 8192 bájtos korlátját ez nem módosítja. Minden warning aktív.
    target_compile_options(ui_preview PRIVATE -O3 -Werror=frame-larger-than=16384)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # GCC 13 + ASan a flow_t.points első skalártagját tévesen a teljes
        # tömb határának veszi (4 bájt, valójában 2×8 bájt). A figyelmeztetés
        # látható marad; a memóriahozzáférést a műszerezett LVGL is ellenőrzi.
        # Ez kizárólag az opcionális host ASan-build eltérő diagnosztikája.
        target_compile_options(ui_preview PRIVATE -Wno-error=stringop-overread)
    endif()
endif()
target_link_libraries(ui_preview PRIVATE lvgl m z -l:libcjson.so.1)
target_link_options(ui_preview PRIVATE "-Wl,--wrap=app_templates_home_load_for")

if(UNIX)
    add_executable(slave_intercom_preview slave_intercom_preview.c
        "${APP_ROOT}/main/fonts/app_font_20.c" "${APP_ROOT}/main/fonts/app_font_28.c")
    target_include_directories(slave_intercom_preview PRIVATE
        "${APP_ROOT}/tests/audio_stubs" "${APP_ROOT}/tests/stubs" "${APP_ROOT}/main")
    target_link_libraries(slave_intercom_preview PRIVATE lvgl m pthread)
    target_compile_options(slave_intercom_preview PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    if(ZOLEE_PREVIEW_ASAN)
        target_compile_options(slave_intercom_preview PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(slave_intercom_preview PRIVATE -fsanitize=address,undefined)
    endif()
endif()
