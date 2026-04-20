#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "vizkey_matrix.h"

typedef enum {
    VIZKEY_ACTION_NONE = 0,
    VIZKEY_ACTION_KEYBOARD,
    VIZKEY_ACTION_CONSUMER,
    VIZKEY_ACTION_MACRO,
} vizkey_action_type_t;

typedef struct {
    vizkey_action_type_t type;
    bool pressed;
    union {
        struct {
            uint8_t modifiers;
            uint8_t keycode;
        } keyboard;
        struct {
            uint16_t usage;
        } consumer;
        struct {
            uint16_t macro_id;
        } macro;
    } data;
} vizkey_action_t;

esp_err_t vizkey_profiles_init(void);
esp_err_t vizkey_profiles_map_event(const vizkey_matrix_event_t *event, vizkey_action_t *out_action);
esp_err_t vizkey_profiles_save_active(uint8_t profile_id);
esp_err_t vizkey_profiles_load_active(uint8_t *profile_id);

esp_err_t vizkey_profiles_fs_init(void);
esp_err_t vizkey_profiles_fs_write_macro(uint16_t macro_id, const void *blob, size_t len);
esp_err_t vizkey_profiles_fs_read_macro(uint16_t macro_id, void *blob, size_t len, size_t *bytes_read);
