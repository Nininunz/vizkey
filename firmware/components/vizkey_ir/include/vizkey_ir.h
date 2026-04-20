#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t vizkey_ir_init(int tx_gpio, int rx_gpio);
esp_err_t vizkey_ir_send_nec(uint16_t address, uint16_t command);
esp_err_t vizkey_ir_start_rx(void);
