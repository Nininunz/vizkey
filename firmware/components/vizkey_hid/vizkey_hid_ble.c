#include "vizkey_hid.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_log.h"

static const char *TAG = "vizkey_hid_ble";
static const vizkey_hid_transport_t *s_transport;
static bool s_ble_started;
static bool s_ble_connected;
static vizkey_hid_connection_cb_t s_connection_cb;
static void *s_connection_cb_ctx;
static esp_hidd_dev_t *s_hid_dev;
static bool s_adv_data_ready;
static bool s_adv_running;

#define KEYBOARD_REPORT_ID 1
#define CONSUMER_REPORT_ID 2
#define KEYBOARD_REPORT_LEN 8
#define CONSUMER_REPORT_LEN 2

static const uint8_t s_hid_report_map[] = {
    // Keyboard report (ID 1): modifiers(1) + reserved(1) + keycodes(6)
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       //   Usage Minimum (224)
    0x29, 0xE7,       //   Usage Maximum (231)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x05,       //   Usage Maximum (Kana)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x02,       //   Output (Data, Variable, Absolute)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Constant)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array)
    0xC0,             // End Collection

    // Consumer control report (ID 2): single 16-bit usage value
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       // Usage (Consumer Control)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x02,       //   Report ID (2)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0x9C, 0x02, //   Logical Maximum (0x029C)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0x9C, 0x02, //   Usage Maximum (0x029C)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x01,       //   Report Count (1)
    0x81, 0x00,       //   Input (Data, Array)
    0xC0,             // End Collection
};

static esp_hid_raw_report_map_t s_report_maps[] = {
    {
        .data = s_hid_report_map,
        .len = sizeof(s_hid_report_map),
    },
};

static esp_hid_device_config_t s_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "VizKey",
    .manufacturer_name = "VizKey",
    .serial_number = "0000000001",
    .report_maps = s_report_maps,
    .report_maps_len = 1,
};

#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
static esp_ble_adv_params_t s_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
#elif CONFIG_BT_BLE_50_FEATURES_SUPPORTED && CONFIG_BT_BLE_50_EXTEND_ADV_EN
#define BLE_EXT_ADV_INSTANCE 0
#define BLE_EXT_ADV_SET_COUNT 1

static uint8_t s_ext_adv_raw_data[] = {
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    0x02, ESP_BLE_AD_TYPE_TX_PWR, 0x00,
    0x03, ESP_BLE_AD_TYPE_16SRV_CMPL, 0x12, 0x18,
    0x03, ESP_BLE_AD_TYPE_APPEARANCE, 0xC1, 0x03,
    0x07, ESP_BLE_AD_TYPE_NAME_CMPL, 'V', 'i', 'z', 'K', 'e', 'y',
};

static esp_ble_gap_ext_adv_t s_ext_adv[BLE_EXT_ADV_SET_COUNT] = {
    [0] = {
        .instance = BLE_EXT_ADV_INSTANCE,
        .duration = 0,
        .max_events = 0,
    },
};

static esp_ble_gap_ext_adv_params_t s_ext_adv_params = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
    .interval_min = ESP_BLE_GAP_ADV_ITVL_MS(20),
    .interval_max = ESP_BLE_GAP_ADV_ITVL_MS(30),
    .channel_map = ADV_CHNL_ALL,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
    .primary_phy = ESP_BLE_GAP_PHY_1M,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_1M,
    .sid = 0,
    .scan_req_notif = false,
};
#endif

static void vizkey_ble_set_connection_state(bool connected)
{
    if (s_ble_connected == connected) {
        return;
    }

    s_ble_connected = connected;
    ESP_LOGI(TAG, "BLE link state: %s", connected ? "connected" : "disconnected");

    if (s_connection_cb != NULL) {
        s_connection_cb(connected, s_connection_cb_ctx);
    }
}

static void vizkey_ble_start_advertising_if_ready(void)
{
    if (!s_ble_started || s_ble_connected || !s_adv_data_ready || s_adv_running) {
        return;
    }

    esp_err_t adv_err = ESP_ERR_NOT_SUPPORTED;
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
    adv_err = esp_ble_gap_start_advertising(&s_adv_params);
#elif CONFIG_BT_BLE_50_FEATURES_SUPPORTED && CONFIG_BT_BLE_50_EXTEND_ADV_EN
    adv_err = esp_ble_gap_ext_adv_start(BLE_EXT_ADV_SET_COUNT, s_ext_adv);
#endif
    if (adv_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start BLE advertising: %s", esp_err_to_name(adv_err));
    } else {
        s_adv_running = true;
    }
}

static void vizkey_ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        s_adv_data_ready = true;
        vizkey_ble_start_advertising_if_ready();
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param != NULL && param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            s_adv_running = false;
            ESP_LOGW(TAG, "BLE advertising start failed: status=%u", (unsigned)param->adv_start_cmpl.status);
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        s_adv_running = false;
        if (param != NULL && param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "BLE advertising stop failed: status=%u", (unsigned)param->adv_stop_cmpl.status);
        }
        break;
#elif CONFIG_BT_BLE_50_FEATURES_SUPPORTED && CONFIG_BT_BLE_50_EXTEND_ADV_EN
    case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:
        if (param == NULL || param->ext_adv_set_params.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(
                TAG,
                "BLE ext adv params failed: status=%u",
                (unsigned)(param != NULL ? param->ext_adv_set_params.status : ESP_BT_STATUS_FAIL));
            break;
        }
        if (esp_ble_gap_config_ext_adv_data_raw(
                BLE_EXT_ADV_INSTANCE, sizeof(s_ext_adv_raw_data), s_ext_adv_raw_data) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to configure BLE ext advertising data");
        }
        break;
    case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:
        if (param != NULL && param->ext_adv_data_set.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_data_ready = true;
            vizkey_ble_start_advertising_if_ready();
        } else {
            ESP_LOGW(
                TAG,
                "BLE ext adv data set failed: status=%u",
                (unsigned)(param != NULL ? param->ext_adv_data_set.status : ESP_BT_STATUS_FAIL));
        }
        break;
    case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
        if (param != NULL && param->ext_adv_start.status == ESP_BT_STATUS_SUCCESS) {
            s_adv_running = true;
        } else {
            s_adv_running = false;
            ESP_LOGW(
                TAG,
                "BLE ext adv start failed: status=%u",
                (unsigned)(param != NULL ? param->ext_adv_start.status : ESP_BT_STATUS_FAIL));
        }
        break;
    case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:
        s_adv_running = false;
        if (param != NULL && param->ext_adv_stop.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "BLE ext adv stop failed: status=%u", (unsigned)param->ext_adv_stop.status);
        }
        break;
    case ESP_GAP_BLE_ADV_TERMINATED_EVT:
        s_adv_running = false;
        break;
#endif
    case ESP_GAP_BLE_SEC_REQ_EVT:
        if (param != NULL) {
            (void)esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        }
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (param != NULL && param->ble_security.auth_cmpl.success) {
            ESP_LOGI(TAG, "BLE link authenticated");
        } else if (param != NULL) {
            ESP_LOGW(TAG, "BLE authentication failed: reason=0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
}

static void vizkey_ble_hidd_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID device started");
        vizkey_ble_start_advertising_if_ready();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HID host connected");
        s_adv_running = false;
        vizkey_ble_set_connection_state(true);
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        if (param != NULL) {
            ESP_LOGI(
                TAG,
                "HID host disconnected: %s",
                esp_hid_disconnect_reason_str(esp_hidd_dev_transport_get(param->disconnect.dev), param->disconnect.reason));
        } else {
            ESP_LOGI(TAG, "HID host disconnected");
        }
        vizkey_ble_set_connection_state(false);
        vizkey_ble_start_advertising_if_ready();
        break;
    case ESP_HIDD_STOP_EVENT:
        ESP_LOGI(TAG, "HID device stopped");
        break;
    default:
        break;
    }
}

static esp_err_t vizkey_ble_init_stack(void)
{
    esp_err_t err;

    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_bt_controller_mem_release failed: %s", esp_err_to_name(err));
    }

    esp_bt_controller_status_t controller_status = esp_bt_controller_get_status();
    if (controller_status == ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        err = esp_bt_controller_init(&bt_cfg);
        if (err != ESP_OK) {
            return err;
        }
        controller_status = esp_bt_controller_get_status();
    }

    if (controller_status == ESP_BT_CONTROLLER_STATUS_INITED) {
        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    } else if (controller_status != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_bluedroid_status_t bluedroid_status = esp_bluedroid_get_status();
    if (bluedroid_status == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
        err = esp_bluedroid_init_with_cfg(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        bluedroid_status = esp_bluedroid_get_status();
    }

    if (bluedroid_status == ESP_BLUEDROID_STATUS_INITIALIZED) {
        err = esp_bluedroid_enable();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    } else if (bluedroid_status != ESP_BLUEDROID_STATUS_ENABLED) {
        return ESP_ERR_INVALID_STATE;
    }

    err = esp_ble_gap_register_callback(vizkey_ble_gap_event_handler);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (err != ESP_OK) {
        return err;
    }

    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req))) != ESP_OK) {
        return err;
    }
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap))) != ESP_OK) {
        return err;
    }
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size))) != ESP_OK) {
        return err;
    }
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key))) != ESP_OK) {
        return err;
    }
    if ((err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key))) != ESP_OK) {
        return err;
    }

    if ((err = esp_ble_gap_set_device_name(s_hid_config.device_name)) != ESP_OK) {
        return err;
    }

    s_adv_data_ready = false;
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
    const uint8_t hid_service_uuid128[] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    esp_ble_adv_data_t adv_data = {
        .set_scan_rsp = false,
        .include_name = true,
        .include_txpower = true,
        .min_interval = ESP_BLE_GAP_CONN_ITVL_MS(7.5),
        .max_interval = ESP_BLE_GAP_CONN_ITVL_MS(20),
        .appearance = ESP_HID_APPEARANCE_KEYBOARD,
        .manufacturer_len = 0,
        .p_manufacturer_data = NULL,
        .service_data_len = 0,
        .p_service_data = NULL,
        .service_uuid_len = sizeof(hid_service_uuid128),
        .p_service_uuid = (uint8_t *)hid_service_uuid128,
        .flag = 0x06,
    };
    if ((err = esp_ble_gap_config_adv_data(&adv_data)) != ESP_OK) {
        return err;
    }
#elif CONFIG_BT_BLE_50_FEATURES_SUPPORTED && CONFIG_BT_BLE_50_EXTEND_ADV_EN
    if ((err = esp_ble_gap_ext_adv_set_params(BLE_EXT_ADV_INSTANCE, &s_ext_adv_params)) != ESP_OK) {
        return err;
    }
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif

    err = esp_hidd_dev_init(&s_hid_config, ESP_HID_TRANSPORT_BLE, vizkey_ble_hidd_event_handler, &s_hid_dev);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t vizkey_ble_deinit_stack(void)
{
    esp_err_t first_err = ESP_OK;

    if (s_hid_dev != NULL) {
        esp_err_t err = esp_hidd_dev_deinit(s_hid_dev);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
        s_hid_dev = NULL;
    }

    s_adv_data_ready = false;
    s_adv_running = false;

    esp_bluedroid_status_t bluedroid_status = esp_bluedroid_get_status();
    if (bluedroid_status == ESP_BLUEDROID_STATUS_ENABLED) {
        esp_err_t err = esp_bluedroid_disable();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && first_err == ESP_OK) {
            first_err = err;
        }
        bluedroid_status = esp_bluedroid_get_status();
    }
    if (bluedroid_status == ESP_BLUEDROID_STATUS_INITIALIZED) {
        esp_err_t err = esp_bluedroid_deinit();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && first_err == ESP_OK) {
            first_err = err;
        }
    }

    esp_bt_controller_status_t controller_status = esp_bt_controller_get_status();
    if (controller_status == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_err_t err = esp_bt_controller_disable();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && first_err == ESP_OK) {
            first_err = err;
        }
        controller_status = esp_bt_controller_get_status();
    }
    if (controller_status == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_err_t err = esp_bt_controller_deinit();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && first_err == ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}

static esp_err_t vizkey_ble_start(void)
{
#if CONFIG_BT_ENABLED && CONFIG_BT_BLUEDROID_ENABLED
    if (s_ble_started) {
        return ESP_OK;
    }

    s_ble_started = true;
    esp_err_t err = vizkey_ble_init_stack();
    if (err != ESP_OK) {
        s_ble_started = false;
        (void)vizkey_ble_deinit_stack();
        return err;
    }

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
    vizkey_ble_set_connection_state(false);
    (void)vizkey_ble_deinit_stack();
    ESP_LOGI(TAG, "BLE HID transport stop requested (standby)");
    return ESP_OK;
}

static esp_err_t vizkey_ble_send_keyboard(const uint8_t report[8])
{
    if (!s_ble_started || !s_ble_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    return esp_hidd_dev_input_set(
        s_hid_dev,
        0,
        KEYBOARD_REPORT_ID,
        (uint8_t *)report,
        KEYBOARD_REPORT_LEN);
}

static esp_err_t vizkey_ble_send_consumer(uint16_t usage, bool pressed)
{
    if (!s_ble_started || !s_ble_connected || s_hid_dev == NULL) {
        return ESP_OK;
    }

    uint8_t report[CONSUMER_REPORT_LEN] = {0};
    if (pressed) {
        report[0] = (uint8_t)(usage & 0xFFU);
        report[1] = (uint8_t)((usage >> 8) & 0xFFU);
    }

    return esp_hidd_dev_input_set(
        s_hid_dev,
        0,
        CONSUMER_REPORT_ID,
        report,
        CONSUMER_REPORT_LEN);
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

void vizkey_hid_ble_notify_connection(bool connected)
{
    if (connected && !s_ble_started) {
        ESP_LOGW(TAG, "Ignoring BLE connected notification while transport is stopped");
        return;
    }
    vizkey_ble_set_connection_state(connected);
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

esp_err_t vizkey_hid_set_connection_callback(vizkey_hid_connection_cb_t callback, void *ctx)
{
    s_connection_cb = callback;
    s_connection_cb_ctx = ctx;
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
