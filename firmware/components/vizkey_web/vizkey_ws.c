#include "vizkey_web.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static vizkey_web_ws_handler_t s_ws_handler;
static const char *TAG = "vizkey_web_ws";

#if CONFIG_HTTPD_WS_SUPPORT
static esp_err_t vizkey_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };

    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &frame, 0), "vizkey_web_ws", "recv ws frame meta failed");
    if (frame.len > 0) {
        frame.payload = calloc(1, frame.len + 1);
        if (frame.payload == NULL) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) {
            free(frame.payload);
            return err;
        }
    }

    if (s_ws_handler != NULL) {
        (void)s_ws_handler((const char *)frame.payload, frame.len);
    }

    static const char ack[] = "{\"ok\":true}";
    httpd_ws_frame_t resp = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)ack,
        .len = sizeof(ack) - 1,
    };

    esp_err_t send_err = httpd_ws_send_frame(req, &resp);
    free(frame.payload);
    return send_err;
}

esp_err_t vizkey_web_set_ws_handler(vizkey_web_ws_handler_t handler)
{
    s_ws_handler = handler;
    return ESP_OK;
}

esp_err_t vizkey_web_ws_register(httpd_handle_t server)
{
    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = vizkey_ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };
    return httpd_register_uri_handler(server, &ws_uri);
}
#else
esp_err_t vizkey_web_set_ws_handler(vizkey_web_ws_handler_t handler)
{
    s_ws_handler = handler;
    return ESP_OK;
}

esp_err_t vizkey_web_ws_register(httpd_handle_t server)
{
    (void)server;
    ESP_LOGW(TAG, "CONFIG_HTTPD_WS_SUPPORT is disabled; /ws endpoint not registered");
    return ESP_OK;
}
#endif
