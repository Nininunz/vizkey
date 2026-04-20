#include <stdbool.h>

#include "app_config.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "vizkey_backlight.h"
#include "vizkey_hid.h"
#include "vizkey_ir.h"
#include "vizkey_matrix.h"
#include "vizkey_profiles.h"
#include "vizkey_web.h"

static const char *TAG = "vizkey_main";

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

void app_main(void)
{
    ESP_LOGI(TAG, "Booting %s", VIZKEY_DEVICE_NAME);

    ESP_ERROR_CHECK(vizkey_init_nvs());
    ESP_ERROR_CHECK(vizkey_init_network_stack());
    ESP_ERROR_CHECK(vizkey_profiles_init());

    ESP_ERROR_CHECK(vizkey_hid_set_transport(vizkey_hid_ble_transport()));
    ESP_ERROR_CHECK(vizkey_hid_start());

    ESP_ERROR_CHECK(vizkey_backlight_init(VIZKEY_BACKLIGHT_GPIO, VIZKEY_BACKLIGHT_MAX_LEVEL));
    ESP_ERROR_CHECK(vizkey_ir_init(VIZKEY_IR_TX_GPIO, VIZKEY_IR_RX_GPIO));
    ESP_ERROR_CHECK(vizkey_web_start());

    ESP_ERROR_CHECK(vizkey_matrix_init());
    ESP_ERROR_CHECK(vizkey_matrix_set_callback(vizkey_on_matrix_event, NULL));

    ESP_LOGI(TAG, "VizKey scaffold initialized");
}
