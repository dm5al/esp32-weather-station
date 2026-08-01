idf_component_register(
    SRCS
        "main.c"
        "bsp/ch422g.c"
        "bsp/board.c"
        "bsp/lcd_timing.c"
        "bsp/lcd_console.c"
        "net/wifi_manager.c"
        "net/http_get.c"
        "net/geolocate.c"
        "net/weather.c"
        "net/holidays.c"
        "ui/i18n.c"
        "ui/ui.c"
        "ui/ui_settings.c"
        "ui/ui_wifi.c"
        "ui/ui_weather.c"
        "ui/weather_icon.c"
        "ui/fonts/lv_font_ui_12.c"
        "ui/fonts/lv_font_ui_14.c"
        "ui/fonts/lv_font_ui_16.c"
        "ui/fonts/lv_font_ui_18.c"
        "ui/fonts/lv_font_ui_20.c"
        "ui/fonts/lv_font_ui_24.c"
        "ui/fonts/lv_font_ui_28.c"
        "ui/fonts/lv_font_ui_48.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_lcd
        esp_driver_i2c
        esp_driver_gpio
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp-tls
        esp_http_client
        json
        mbedtls
        lwip
        console
        esp_driver_uart
)

# lv_font_conv output includes <lvgl/lvgl.h> unless told otherwise; the ESP-IDF
# LVGL component puts lvgl.h directly on the include path.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
