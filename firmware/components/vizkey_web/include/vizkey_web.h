#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef esp_err_t (*vizkey_web_ws_handler_t)(const char *payload, size_t len);

esp_err_t vizkey_web_start(void);
esp_err_t vizkey_web_stop(void);
esp_err_t vizkey_web_set_ws_handler(vizkey_web_ws_handler_t handler);

esp_err_t vizkey_web_ws_register(httpd_handle_t server);
