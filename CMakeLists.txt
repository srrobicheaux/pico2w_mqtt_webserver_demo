# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)

project(HA_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(HA_pico
    networking.c
    main.c
    io_manager.c
    flash_manager.c
    cJSON.c
    webserver.c
    mqtt_manager.c
)

# Pull in lwip configuration
target_include_directories(HA_pico PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(HA_pico PRIVATE build/${CMAKE_CURRENT_BINARY_DIR})   # Important for build/ headers

# Link the required libraries
target_link_libraries(HA_pico
    pico_stdlib
    pico_cyw43_arch_lwip_poll
    hardware_adc
    hardware_flash
    hardware_sync
    hardware_watchdog
    pico_lwip_mqtt
)

# Enable USB printing instead of UART (optional but good for debugging)
pico_enable_stdio_usb(HA_pico 1)
pico_enable_stdio_uart(HA_pico 0)

pico_add_extra_outputs(HA_pico)