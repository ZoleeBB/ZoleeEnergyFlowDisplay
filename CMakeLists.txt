# Minimális CMake verzió az ESP-IDF projektkonfigurációhoz.
cmake_minimum_required(VERSION 3.16)

option(ZOLEE_SIMULATOR "Build the desktop UI simulator instead of ESP firmware" OFF)
if(ZOLEE_SIMULATOR)
    project(zolee_desktop C CXX)
    enable_testing()
    add_subdirectory(simulator)
    return()
endif()

# Az ESP-IDF szabványos projekt-CMake rendszerének betöltése.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware verzió, amely az ESP alkalmazásleíróban és a webes információs oldalon is megjelenik.
if(NOT DEFINED PROJECT_VER AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/version.txt")
    set(PROJECT_VER "0.1.0")
endif()

# A firmware projekt neve.
project(zolee_energy_flow_display)


include("${CMAKE_CURRENT_LIST_DIR}/cmake/display_psram.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/cmake/hosted_rx.cmake")

# A tényleges Master ELF-ben is a megfigyelőn át kell érkeznie az ARP-nak,
# majd minden keretnek tovább kell jutnia az eredeti lwIP-vevőhöz.
idf_build_get_property(arp_check_python PYTHON)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND "${arp_check_python}" "${CMAKE_CURRENT_LIST_DIR}/tools/check_inverter_arp_link.py"
            "${CMAKE_OBJDUMP}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>"
    VERBATIM)
