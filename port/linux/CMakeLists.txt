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

# ---- display size -----------------------------------------------------------
#
# The interface is laid out from these rather than from fixed coordinates, so it
# fills whatever is attached instead of sitting in a corner of it. 1024x600 is
# the default because that is the panel this port was written for; the ESP32
# firmware keeps its own 800x480 from bsp/board.h.
#
# Fonts do not scale with this - a much larger screen gets more whitespace, not
# larger text. Somewhere past about 1280 wide that will start to look sparse and
# the font set would want a second size rather than the layout another tweak.
set(DISPLAY_W "1024" CACHE STRING "Display width in pixels")
set(DISPLAY_H "600"  CACHE STRING "Display height in pixels")

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

#
# lv_conf.h is found through the include path, not through LV_CONF_PATH.
#
# LVGL passes that variable to the compiler unquoted:
#
#   target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=${LV_CONF_PATH})
#
# so the path arrives at the preprocessor as bare tokens rather than as a string
# literal, and any token in it that happens to be a predefined macro is
# substituted. GCC predefines "linux" as 1 on Linux targets, so this directory -
# port/linux - turned the path into port/1/lv_conf.h and the build failed on a
# file nobody had ever named. LV_CONF_INCLUDE_SIMPLE makes LVGL include
# "lv_conf.h" plainly and find ours on the include path, where no expansion can
# happen.
# Cleared rather than merely left unset: an earlier version of this file wrote
# LV_CONF_PATH into the cache, and a build tree configured by it keeps handing
# the poisoned value to LVGL no matter what this file says now.
unset(LV_CONF_PATH CACHE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Turn on the one backend that was asked for, in LVGL's build as well as ours -
# lv_conf.h defaults all of them to off, so anything not named here is not
# compiled and its libraries are not needed.
if(BSP_BACKEND STREQUAL "DRM")
    set(LV_BACKEND_DEF LV_USE_LINUX_DRM=1)
elseif(BSP_BACKEND STREQUAL "FBDEV")
    set(LV_BACKEND_DEF LV_USE_LINUX_FBDEV=1)
else()
    set(LV_BACKEND_DEF LV_USE_SDL=1)
endif()
target_compile_definitions(lvgl PUBLIC ${LV_BACKEND_DEF})

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
    ${PROJECT_ROOT}/main/ui/weather_icon.c
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

target_compile_definitions(esp32-weather PRIVATE
    # pthread_setname_np and friends are GNU extensions, and -std=gnu11 alone
    # does not expose them. Absent, they compile as implicit declarations and
    # fail on any compiler that treats that as the error it now is.
    _GNU_SOURCE
    BSP_BACKEND_${BSP_BACKEND}
    BSP_LCD_H_RES=${DISPLAY_W}
    BSP_LCD_V_RES=${DISPLAY_H}
)

# -Wall and -Wextra, but not -Werror: this has to build on whatever gcc the
# owner's Raspberry Pi OS image happens to ship, and a new compiler's new
# warning should not stop a weather display from being built.
target_compile_options(esp32-weather PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- making the link fit on a Pi Zero W -------------------------------------
#
# During the first link on real hardware the machine stopped responding: the
# kernel kept answering pings while userspace was no longer being scheduled -
# alive, unreachable, and needing the power pulled.
#
# The Pi Zero W has 426 MB and, on this image, no disk swap at all. What free(1)
# reports is zram: compressed RAM, which buys a little by squeezing pages but
# cannot supply memory that is not there, and costs CPU to do it. ld maps the
# whole of LVGL's archive at once, which is exactly the shape of demand zram
# handles worst.
#
# (The link then failed on a missing source file rather than on memory, so these
# two problems were found together and are not the same problem. The mitigations
# below stand on their own: this machine has no headroom to spare.)
#
#   --no-keep-memory   tells ld to reread from disk rather than hold everything
#                      in memory. Slower, and slow is available where memory is
#                      not.
#   --gc-sections      with -ffunction-sections/-fdata-sections, drops what is
#                      never called. LVGL compiles every built-in font and every
#                      widget; this project uses its own fonts and a handful of
#                      widgets, so most of that archive is dead weight.
target_compile_options(esp32-weather PRIVATE -ffunction-sections -fdata-sections)
target_compile_options(lvgl PRIVATE -ffunction-sections -fdata-sections)
target_link_options(esp32-weather PRIVATE -Wl,--no-keep-memory -Wl,--gc-sections)

# ---- dependencies -----------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

target_include_directories(esp32-weather PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(esp32-weather PRIVATE lvgl ${CURL_LIBRARIES} m pthread)

# The backend libraries go to LVGL as well as to us. LVGL compiles the driver
# itself - lv_linux_drm.c, lv_sdl_window.c - so it needs the headers too, and
# libdrm's xf86drm.h includes <drm.h> from a directory only pkg-config knows
# about. Giving them only to our own target left LVGL failing on a header it was
# never told where to find.
if(BSP_BACKEND STREQUAL "DRM")
    pkg_check_modules(DRM REQUIRED libdrm)
    target_include_directories(esp32-weather PRIVATE ${DRM_INCLUDE_DIRS})
    target_link_libraries(esp32-weather PRIVATE ${DRM_LIBRARIES})
    target_include_directories(lvgl PUBLIC ${DRM_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${DRM_LIBRARIES})
elseif(BSP_BACKEND STREQUAL "SDL")
    pkg_check_modules(SDL2 REQUIRED sdl2)
    target_include_directories(esp32-weather PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(esp32-weather PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

# cJSON: the firmware gets it from ESP-IDF, so it has to come from the system
# here. It is in libcjson-dev on Debian and Raspberry Pi OS.
pkg_check_modules(CJSON REQUIRED libcjson)
target_include_directories(esp32-weather PRIVATE ${CJSON_INCLUDE_DIRS})
target_link_libraries(esp32-weather PRIVATE ${CJSON_LIBRARIES})

install(TARGETS esp32-weather RUNTIME DESTINATION bin)
