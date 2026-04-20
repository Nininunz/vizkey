#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    VIZKEY_BACKLIGHT_DRIVER_LEDC = 0,
    VIZKEY_BACKLIGHT_DRIVER_WS2812 = 1,
} vizkey_backlight_driver_t;

esp_err_t vizkey_backlight_init(
    int gpio_num,
    uint8_t max_level,
    bool output_invert,
    vizkey_backlight_driver_t driver);
esp_err_t vizkey_backlight_set_level(uint8_t level);
esp_err_t vizkey_backlight_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t vizkey_backlight_set_enabled(bool enabled);
