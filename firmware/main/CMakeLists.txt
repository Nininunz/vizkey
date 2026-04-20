idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        vizkey_backlight
        vizkey_hid
        vizkey_ir
        vizkey_matrix
        vizkey_profiles
        vizkey_web
        nvs_flash
)
