# Linux build of the ESP32 weather station, for a Raspberry Pi driving HDMI.
#
# The application, the network clients and the whole interface are the same
# sources the ESP-IDF firmware builds. What differs is underneath them: LVGL
# talks to DRM or the framebuffer instead of an RGB panel, HTTP goes through
# libcurl instead of esp_http_client, settings land in a file instead of NVS,
# and the radio belongs to the operating system.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j$(nproc)
#
# On a Pi Zero W "-j$(nproc)" is "-j1" and the build takes a while. Nothing here
# needs to be built on the Pi itself, but building there is far simpler than
# arranging a cross toolchain, and it only has to happen once.

cmake_minimum_required(VERSION 3.16)
project(esp32-weather-linux C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ---- display backend --------------------------------------------------------
#
# DRM is the default because Raspberry Pi OS has used the KMS driver since
# Bullseye and it is what a current Pi will have. FBDEV remains for older images
# and for anything running with vc4-fkms-v3d; SDL is for developing this on a
# desktop rather than on the Pi.
set(BSP_BACKEND "DRM" CACHE STRING "Display backend: DRM, FBDEV or SDL")
set_property(CACHE BSP_BACKEND PROPERTY STRINGS DRM FBDEV SDL)

# ---- LVGL -------------------------------------------------------------------
#
# Pinned to the same 9.2 the firmware uses through the component manager. A
# different minor version between the two targets would mean the interface code
# is no longer shared in any meaningful sense.
include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
    GIT_SHALLOW    TRUE
)

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE FILEPATH "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

# ---- shared application sources ---------------------------------------------
#
# Listed rather than globbed: a file appearing in main/ that this build has not
# been taught about should be a link error someone reads, not a silent omission.
set(SHARED_SOURCES
    ${PROJECT_ROOT}/main/main.c
    ${PROJECT_ROOT}/main/net/weather.c
    ${PROJECT_ROOT}/main/net/geolocate.c
    ${PROJECT_ROOT}/main/net/holidays.c
    ${PROJECT_ROOT}/main/ui/ui.c
    ${PROJECT_ROOT}/main/ui/ui_weather.c
    ${PROJECT_ROOT}/main/ui/ui_settings.c
    ${PROJECT_ROOT}/main/ui/ui_wifi.c
    ${PROJECT_ROOT}/main/ui/i18n.c
)
file(GLOB FONT_SOURCES ${PROJECT_ROOT}/main/ui/fonts/*.c)

# Deliberately not built here:
#   main/bsp/board.c        RGB panel, GT911, CH422G - none of it exists
#   main/bsp/ch422g.c       an I2C expander on a board that is not this one
#   main/bsp/lcd_console.c  tunes panel timings HDMI does not have
#   main/bsp/lcd_timing.c   likewise
#   main/net/http_get.c     replaced by libcurl
#   main/net/wifi_manager.c replaced by the host's networking

set(PORT_SOURCES
    main_linux.c
    bsp_linux.c
    stubs_linux.c
    http_get_curl.c
    wifi_manager_linux.c
    compat/compat_misc.c
    compat/nvs_compat.c
    compat/freertos_compat.c
)

add_executable(esp32-weather ${SHARED_SOURCES} ${FONT_SOURCES} ${PORT_SOURCES})

target_include_directories(esp32-weather PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compat   # esp_err.h, esp_log.h, nvs.h, freertos/
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_ROOT}/main
)

target_compile_definitions(esp32-weather PRIVATE BSP_BACKEND_${BSP_BACKEND})

# -Wall and -Wextra, but not -Werror: this has to build on whatever gcc the
# owner's Raspberry Pi OS image happens to ship, and a new compiler's new
# warning should not stop a weather display from being built.
target_compile_options(esp32-weather PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- dependencies -----------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

target_include_directories(esp32-weather PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(esp32-weather PRIVATE lvgl ${CURL_LIBRARIES} m pthread)

if(BSP_BACKEND STREQUAL "DRM")
    pkg_check_modules(DRM REQUIRED libdrm)
    target_include_directories(esp32-weather PRIVATE ${DRM_INCLUDE_DIRS})
    target_link_libraries(esp32-weather PRIVATE ${DRM_LIBRARIES})
elseif(BSP_BACKEND STREQUAL "SDL")
    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(esp32-weather PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(esp32-weather PRIVATE ${SDL2_LIBRARIES})
endif()

# cJSON: the firmware gets it from ESP-IDF, so it has to come from the system
# here. It is in libcjson-dev on Debian and Raspberry Pi OS.
pkg_check_modules(CJSON REQUIRED libcjson)
target_include_directories(esp32-weather PRIVATE ${CJSON_INCLUDE_DIRS})
target_link_libraries(esp32-weather PRIVATE ${CJSON_LIBRARIES})

install(TARGETS esp32-weather RUNTIME DESTINATION bin)
