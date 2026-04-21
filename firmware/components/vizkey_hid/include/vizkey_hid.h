#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "vizkey_profiles.h"

typedef struct {
    const char *name;
    esp_err_t (*start)(void);
    esp_err_t (*stop)(void);
    esp_err_t (*send_keyboard_report)(const uint8_t report[8]);
    esp_err_t (*send_consumer_report)(uint16_t usage, bool pressed);
} vizkey_hid_transport_t;

typedef void (*vizkey_hid_connection_cb_t)(bool connected, void *ctx);

esp_err_t vizkey_hid_set_transport(const vizkey_hid_transport_t *transport);
esp_err_t vizkey_hid_set_connection_callback(vizkey_hid_connection_cb_t callback, void *ctx);
esp_err_t vizkey_hid_start(void);
esp_err_t vizkey_hid_stop(void);
esp_err_t vizkey_hid_send_action(const vizkey_action_t *action);

const vizkey_hid_transport_t *vizkey_hid_ble_transport(void);
void vizkey_hid_ble_notify_connection(bool connected);

void vizkey_hid_build_keyboard_report(uint8_t modifiers, uint8_t keycode, bool pressed, uint8_t report[8]);
