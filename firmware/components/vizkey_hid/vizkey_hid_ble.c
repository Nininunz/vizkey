#include "vizkey_hid.h"

#include "esp_log.h"

static const char *TAG = "vizkey_hid_ble";
static const vizkey_hid_transport_t *s_transport;
static bool s_ble_started;

static esp_err_t vizkey_ble_start(void)
{
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
    if (s_ble_started) {
        return ESP_OK;
    }
    s_ble_started = true;
    ESP_LOGI(TAG, "BLE HID transport start requested (advertising window open)");
    return ESP_OK;
#else
    ESP_LOGE(TAG, "BLE HID requires BT + Bluedroid in sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t vizkey_ble_stop(void)
{
    if (!s_ble_started) {
        return ESP_OK;
    }
    s_ble_started = false;
    ESP_LOGI(TAG, "BLE HID transport stop requested (standby)");
    return ESP_OK;
}

static esp_err_t vizkey_ble_send_keyboard(const uint8_t report[8])
{
    (void)report;
    if (!s_ble_started) {
        return ESP_OK;
    }
    // TODO: Wire this to the real esp_hidd_dev_input_set path.
    return ESP_OK;
}

static esp_err_t vizkey_ble_send_consumer(uint16_t usage, bool pressed)
{
    (void)usage;
    (void)pressed;
    if (!s_ble_started) {
        return ESP_OK;
    }
    // TODO: Wire this to the BLE HID consumer report map.
    return ESP_OK;
}

static const vizkey_hid_transport_t s_ble_transport = {
    .name = "ble_bluedroid",
    .start = vizkey_ble_start,
    .stop = vizkey_ble_stop,
    .send_keyboard_report = vizkey_ble_send_keyboard,
    .send_consumer_report = vizkey_ble_send_consumer,
};

const vizkey_hid_transport_t *vizkey_hid_ble_transport(void)
{
    return &s_ble_transport;
}

esp_err_t vizkey_hid_set_transport(const vizkey_hid_transport_t *transport)
{
    if (transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (transport->send_keyboard_report == NULL || transport->start == NULL || transport->stop == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_transport = transport;
    ESP_LOGI(TAG, "Selected HID transport: %s", transport->name);
    return ESP_OK;
}

esp_err_t vizkey_hid_start(void)
{
    if (s_transport == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_transport->start();
}

esp_err_t vizkey_hid_stop(void)
{
    if (s_transport == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_transport->stop();
}

esp_err_t vizkey_hid_send_action(const vizkey_action_t *action)
{
    if (action == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_transport == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (action->type) {
    case VIZKEY_ACTION_NONE:
        return ESP_OK;
    case VIZKEY_ACTION_KEYBOARD: {
        uint8_t report[8];
        vizkey_hid_build_keyboard_report(
            action->data.keyboard.modifiers,
            action->data.keyboard.keycode,
            action->pressed,
            report);
        return s_transport->send_keyboard_report(report);
    }
    case VIZKEY_ACTION_CONSUMER:
        if (s_transport->send_consumer_report == NULL) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        return s_transport->send_consumer_report(action->data.consumer.usage, action->pressed);
    case VIZKEY_ACTION_MACRO:
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}
