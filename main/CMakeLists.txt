file(GLOB_RECURSE APP_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(CHAIN_CAPTAIN_ASSET_PYTHON
    "${Python3_EXECUTABLE}"
    CACHE FILEPATH
    "Python interpreter used by the Chain Captain asset targets"
)

include("${CMAKE_CURRENT_LIST_DIR}/assets/assets.cmake")

option(CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG "Log display FPS once per second" OFF)

idf_component_register(
    SRCS
        "main.cpp"
        ${APP_SRCS}
        ${CHAIN_CAPTAIN_ASSET_SOURCES}
    INCLUDE_DIRS
        "."
    REQUIRES
        ArduinoJson
        fatfs
        M5GFX
        esp_codec_dev
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_rmt
        esp_driver_spi
        esp_driver_uart
        esp_http_client
        esp_lcd
        esp_lvgl_port
        esp-wifi-connect
        espressif2022__bmi270
        lvgl
        m5stack__m5ioe1
        m5stack__m5pm1
        mooncake
        mooncake_log
        mqtt
        nvs_flash
        smooth_ui_toolkit
        wear_levelling
)

if(CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG=1)
endif()

add_custom_target(generate_assets
    COMMAND "${CHAIN_CAPTAIN_ASSET_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/assets/generate_assets.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
    COMMENT "Generating Chain Captain LVGL assets"
    USES_TERMINAL
)

add_custom_target(check_assets
    COMMAND "${CHAIN_CAPTAIN_ASSET_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/assets/generate_assets.py" --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
    COMMENT "Checking Chain Captain LVGL assets"
    USES_TERMINAL
)
