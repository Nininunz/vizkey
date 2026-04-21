idf_component_register(
    SRCS
        "vizkey_hid_ble.c"
        "vizkey_hid_reports.c"
    INCLUDE_DIRS "include"
    REQUIRES bt esp_hid vizkey_profiles
)
