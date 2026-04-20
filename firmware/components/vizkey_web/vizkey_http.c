#include "vizkey_web.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "vizkey_web_http";
static httpd_handle_t s_server;

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static const char body[] = "VizKey config API";
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t health_get_handler(httpd_req_t *req)
{
    static const char body[] = "{\"ok\":true}";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

esp_err_t vizkey_web_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "http server start failed");

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &root_uri), TAG, "root uri register failed");

    const httpd_uri_t health_uri = {
        .uri = "/health",
        .method = HTTP_GET,
        .handler = health_get_handler,
        .user_ctx = NULL,
    };
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &health_uri), TAG, "health uri register failed");

    ESP_RETURN_ON_ERROR(vizkey_web_ws_register(s_server), TAG, "ws uri register failed");
    ESP_LOGI(TAG, "HTTP config API started");
    return ESP_OK;
}

esp_err_t vizkey_web_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(httpd_stop(s_server), TAG, "http server stop failed");
    s_server = NULL;
    return ESP_OK;
}
