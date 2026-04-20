#include "vizkey_ir.h"

#include "esp_log.h"

static const char *TAG = "vizkey_ir_rx";

esp_err_t vizkey_ir_start_rx(void)
{
    ESP_LOGI(TAG, "IR RX scaffold ready (RMT receiver hookup pending)");
    return ESP_OK;
}
