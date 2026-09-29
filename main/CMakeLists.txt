idf_component_register(
    SRCS "main.c"
         "xigua_app.c"
         "xigua_font_zh16.c"
         "xigua_wifi.c"
         "xigua_wifi_security.c"
         "xigua_wifi_radio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi
             mbedtls nvs_flash json
)
