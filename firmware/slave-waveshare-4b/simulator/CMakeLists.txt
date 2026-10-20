set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LV_BUILD_CONF_PATH "${APP_ROOT}/simulator/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
if(LVGL_SOURCE_DIR)
    add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
else()
    include(FetchContent)
    FetchContent_Declare(lvgl URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz
        URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6)
    FetchContent_MakeAvailable(lvgl)
endif()
set(board "${CMAKE_CURRENT_LIST_DIR}/..")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${board}/tools/assets.py"
    "${CMAKE_CURRENT_BINARY_DIR}/slave_4b_assets.c" COMMAND_ERROR_IS_FATAL ANY)
file(GLOB assets "${board}/assets/*.png" "${APP_ROOT}/main/assets/png/*.png")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${assets} "${board}/tools/assets.py")
add_executable(zolee_slave_4b_simulator simulator.c "${board}/ui/slave_4b_ui.c"
    "${CMAKE_CURRENT_BINARY_DIR}/slave_4b_assets.c"
    "${APP_ROOT}/main/app_ble_protocol.c"
    "${APP_ROOT}/main/fonts/app_font_16.c"
    "${APP_ROOT}/main/fonts/app_font_20.c"
    "${APP_ROOT}/main/fonts/app_font_28.c")
target_include_directories(zolee_slave_4b_simulator PRIVATE "${board}/ui" "${APP_ROOT}/main"
    "${APP_ROOT}/tests/stubs")
target_link_libraries(zolee_slave_4b_simulator PRIVATE lvgl)
if(WIN32)
    target_link_libraries(zolee_slave_4b_simulator PRIVATE user32 gdi32)
else()
    target_link_libraries(zolee_slave_4b_simulator PRIVATE m)
    target_compile_options(zolee_slave_4b_simulator PRIVATE -Wall -Wextra -Werror)
endif()
if(MSVC)
    target_compile_options(zolee_slave_4b_simulator PRIVATE /utf-8 /W3)
    target_compile_definitions(zolee_slave_4b_simulator PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME slave_4b_render_and_protocol COMMAND zolee_slave_4b_simulator --self-test)
