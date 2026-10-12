cmake_minimum_required(VERSION 3.14)
project(chain_captain_simulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG "Log display FPS once per second" OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(SDL2 REQUIRED SDL2)

set(CHAIN_CAPTAIN_ASSET_PYTHON
    "${Python3_EXECUTABLE}"
    CACHE FILEPATH
    "Python interpreter used by the Chain Captain asset targets"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/assets/assets.cmake")

add_custom_target(generate_assets
    COMMAND "${CHAIN_CAPTAIN_ASSET_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/generate_assets.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating Chain Captain LVGL assets"
    USES_TERMINAL
)

add_custom_target(check_assets
    COMMAND "${CHAIN_CAPTAIN_ASSET_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/generate_assets.py" --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking Chain Captain LVGL assets"
    USES_TERMINAL
)

set(M5GFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/M5GFX")
file(GLOB M5GFX_SOURCES
    "${M5GFX_DIR}/src/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/misc/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/panel/*.cpp"
    "${M5GFX_DIR}/src/lgfx/v1/touch/*.cpp"
    "${M5GFX_DIR}/src/lgfx/utility/*.c"
    "${M5GFX_DIR}/src/lgfx/Fonts/efont/*.c"
    "${M5GFX_DIR}/src/lgfx/Fonts/IPA/*.c"
    "${M5GFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp"
)

add_library(M5GFX STATIC ${M5GFX_SOURCES})
target_include_directories(M5GFX PUBLIC
    "${M5GFX_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lvgl"
    "${SDL2_INCLUDE_DIRS}"
)
target_compile_definitions(M5GFX PUBLIC M5GFX_USE_LVGL_FONT=1)
target_link_libraries(M5GFX PUBLIC ${SDL2_LIBRARIES})

set(MOONCAKE_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake)

set(MOONCAKE_LOG_BUILD_EXAMPLE OFF)
add_subdirectory(dependencies/mooncake_log)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF)
add_subdirectory(dependencies/smooth_ui_toolkit)

add_subdirectory(dependencies/ArduinoJson)

set(BUILD_SHARED OFF CACHE BOOL "Build shared libzmq" FORCE)
set(BUILD_STATIC ON CACHE BOOL "Build static libzmq" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build libzmq tests" FORCE)
set(WITH_DOCS OFF CACHE BOOL "Build libzmq documentation" FORCE)
set(WITH_PERF_TOOL OFF CACHE BOOL "Build libzmq performance tools" FORCE)
set(ENABLE_CPACK OFF CACHE BOOL "Enable libzmq packaging" FORCE)
set(ENABLE_DRAFTS OFF CACHE BOOL "Enable libzmq draft APIs" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(dependencies/libzmq)

set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Use LVGL's internal ThorVG" FORCE)
add_subdirectory(dependencies/lvgl)

file(GLOB_RECURSE APP_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)

add_executable(chain_captain_simulator
    utils/sdl_main.cpp
    main.cpp
    ${APP_SRCS}
    ${CHAIN_CAPTAIN_ASSET_SOURCES}
)
target_include_directories(chain_captain_simulator PUBLIC ".")
if(CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG)
    target_compile_definitions(chain_captain_simulator PRIVATE CHAIN_CAPTAIN_DISPLAY_FPS_DEBUG=1)
endif()
target_link_libraries(chain_captain_simulator PUBLIC
    M5GFX
    ArduinoJson
    lvgl
    mooncake
    mooncake_log
    pthread
    smooth_ui_toolkit
    libzmq-static
)
