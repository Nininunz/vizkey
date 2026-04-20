#include "vizkey_ir.h"

#include "esp_log.h"

static const char *TAG = "vizkey_ir_tx";

static int s_tx_gpio = -1;

esp_err_t vizkey_ir_init(int tx_gpio, int rx_gpio)
{
    s_tx_gpio = tx_gpio;
    (void)rx_gpio;
    ESP_LOGI(TAG, "IR TX scaffold initialized on GPIO %d (RMT hookup pending)", s_tx_gpio);
    return ESP_OK;
}

esp_err_t vizkey_ir_send_nec(uint16_t address, uint16_t command)
{
    (void)address;
    (void)command;
    if (s_tx_gpio < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    // TODO: Encode NEC symbols and transmit using RMT.
    return ESP_OK;
}
