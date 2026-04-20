#include "vizkey_matrix.h"

#include "esp_log.h"

static const char *TAG = "vizkey_matrix";

static vizkey_matrix_callback_t s_callback;
static void *s_callback_ctx;

esp_err_t vizkey_matrix_init(void)
{
    ESP_LOGI(TAG, "Matrix scanner init (seed for espressif/keyboard_button)");
    return ESP_OK;
}

esp_err_t vizkey_matrix_set_callback(vizkey_matrix_callback_t callback, void *ctx)
{
    s_callback = callback;
    s_callback_ctx = ctx;
    return ESP_OK;
}

esp_err_t vizkey_matrix_poll(void)
{
    (void)s_callback;
    (void)s_callback_ctx;
    return ESP_OK;
}
