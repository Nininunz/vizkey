idf_component_register(
    SRCS
        "vizkey_http.c"
        "vizkey_ws.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
)
