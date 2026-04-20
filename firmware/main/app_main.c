#include <stdbool.h>
#include <string.h>

#include "app_config.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "vizkey_backlight.h"
#include "vizkey_hid.h"
#include "vizkey_ir.h"
#include "vizkey_matrix.h"
#include "vizkey_profiles.h"
#include "vizkey_web.h"

static const char *TAG = "vizkey_main";
static const uint32_t MATRIX_POLL_INTERVAL_MS = 10;
static const char *CMD_SIM_ON = "matrix.sim.on";
static const char *CMD_SIM_OFF = "matrix.sim.off";
static const char *CMD_SIM_TOGGLE = "matrix.sim.toggle";
static const char *CMD_SIM_STATUS = "matrix.sim.status";

static void vizkey_matrix_task(void *ctx)
{
    (void)ctx;

    while (true) {
        esp_err_t err = vizkey_matrix_poll();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Matrix poll failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(MATRIX_POLL_INTERVAL_MS));
    }
}

static esp_err_t vizkey_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t vizkey_init_network_stack(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t vizkey_init_wifi_ap(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t ap_cfg = {0};
    ap_cfg.ap.channel = VIZKEY_WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = VIZKEY_WIFI_AP_MAX_CONNECTIONS;
    ap_cfg.ap.beacon_interval = 100;

    const bool has_password = VIZKEY_WIFI_AP_PASSWORD[0] != '\0';
    if (has_password) {
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char *)ap_cfg.ap.password, VIZKEY_WIFI_AP_PASSWORD, sizeof(ap_cfg.ap.password));
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    strlcpy((char *)ap_cfg.ap.ssid, VIZKEY_WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(VIZKEY_WIFI_AP_SSID);

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(
        TAG,
        "Wi-Fi AP started: ssid=%s channel=%u max_conn=%u auth=%s",
        VIZKEY_WIFI_AP_SSID,
        (unsigned)ap_cfg.ap.channel,
        (unsigned)ap_cfg.ap.max_connection,
        has_password ? "wpa2" : "open");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Wi-Fi support disabled in this build; local HTTP/WS may be unreachable");
    return ESP_OK;
#endif
}

static void vizkey_on_matrix_event(const vizkey_matrix_event_t *event, void *ctx)
{
    (void)ctx;

    vizkey_action_t action = {0};
    if (vizkey_profiles_map_event(event, &action) != ESP_OK) {
        return;
    }
    if (action.type == VIZKEY_ACTION_NONE) {
        return;
    }
    if (vizkey_hid_send_action(&action) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send HID action for row=%u col=%u", event->row, event->col);
    }
}

static bool vizkey_ws_cmd_equals(const char *payload, size_t len, const char *cmd)
{
    const size_t cmd_len = strlen(cmd);
    return len == cmd_len && memcmp(payload, cmd, cmd_len) == 0;
}

static esp_err_t vizkey_on_ws_command(const char *payload, size_t len)
{
    if (payload == NULL || len == 0U) {
        return ESP_OK;
    }

    if (vizkey_ws_cmd_equals(payload, len, CMD_SIM_ON)) {
        return vizkey_matrix_set_simulation_enabled(true);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_SIM_OFF)) {
        return vizkey_matrix_set_simulation_enabled(false);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_SIM_TOGGLE)) {
        return vizkey_matrix_set_simulation_enabled(!vizkey_matrix_is_simulation_enabled());
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_SIM_STATUS)) {
        ESP_LOGI(
            TAG,
            "Synthetic matrix stream is currently %s",
            vizkey_matrix_is_simulation_enabled() ? "enabled" : "disabled");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown WS command: %.*s", (int)len, payload);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting %s", VIZKEY_DEVICE_NAME);

    ESP_ERROR_CHECK(vizkey_init_nvs());
    ESP_ERROR_CHECK(vizkey_init_network_stack());
    ESP_ERROR_CHECK(vizkey_init_wifi_ap());
    ESP_ERROR_CHECK(vizkey_profiles_init());

    ESP_ERROR_CHECK(vizkey_hid_set_transport(vizkey_hid_ble_transport()));
    ESP_ERROR_CHECK(vizkey_hid_start());

    ESP_ERROR_CHECK(vizkey_backlight_init(VIZKEY_BACKLIGHT_GPIO, VIZKEY_BACKLIGHT_MAX_LEVEL));
    ESP_ERROR_CHECK(vizkey_ir_init(VIZKEY_IR_TX_GPIO, VIZKEY_IR_RX_GPIO));
    ESP_ERROR_CHECK(vizkey_web_set_ws_handler(vizkey_on_ws_command));
    ESP_ERROR_CHECK(vizkey_web_start());

    ESP_ERROR_CHECK(vizkey_matrix_init());
    ESP_ERROR_CHECK(vizkey_matrix_set_callback(vizkey_on_matrix_event, NULL));
    ESP_ERROR_CHECK(
        xTaskCreate(vizkey_matrix_task, "vizkey_matrix", 4096, NULL, 5, NULL) == pdPASS
            ? ESP_OK
            : ESP_ERR_NO_MEM);

    ESP_LOGI(
        TAG,
        "VizKey scaffold initialized (WS commands: matrix.sim.on/off/toggle/status)");
}
