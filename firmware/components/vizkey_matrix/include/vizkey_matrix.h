#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t row;
    uint8_t col;
    bool pressed;
    uint32_t timestamp_ms;
} vizkey_matrix_event_t;

typedef void (*vizkey_matrix_callback_t)(const vizkey_matrix_event_t *event, void *ctx);

esp_err_t vizkey_matrix_init(void);
esp_err_t vizkey_matrix_set_callback(vizkey_matrix_callback_t callback, void *ctx);
esp_err_t vizkey_matrix_poll(void);
esp_err_t vizkey_matrix_inject_event(const vizkey_matrix_event_t *event);
esp_err_t vizkey_matrix_set_simulation_enabled(bool enabled);
bool vizkey_matrix_is_simulation_enabled(void);
