#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t vizkey_backlight_init(int gpio_num, uint8_t max_level);
esp_err_t vizkey_backlight_set_level(uint8_t level);
esp_err_t vizkey_backlight_set_enabled(bool enabled);
