# Minimális CMake verzió az ESP-IDF projektkonfigurációhoz.
cmake_minimum_required(VERSION 3.16)

# Az ESP-IDF szabványos projekt-CMake rendszerének betöltése.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware verzió, amely az ESP alkalmazásleíróban és a webes információs oldalon is megjelenik.
set(PROJECT_VER "0.1.0")

# A firmware projekt neve.
project(zolee_energy_flow_display)
