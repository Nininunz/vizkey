#include "vizkey_profiles.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "vizkey_profiles";
static const char *NVS_NS = "vizkey";
static const char *NVS_KEY_ACTIVE_PROFILE = "active_profile";

esp_err_t vizkey_profiles_init(void)
{
    uint8_t profile_id = 0;
    esp_err_t err = vizkey_profiles_load_active(&profile_id);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(vizkey_profiles_save_active(0), TAG, "Failed to seed active profile");
    } else {
        ESP_RETURN_ON_ERROR(err, TAG, "Failed loading active profile");
    }

    ESP_RETURN_ON_ERROR(vizkey_profiles_fs_init(), TAG, "Failed to initialize LittleFS profile storage");
    ESP_LOGI(TAG, "Profiles initialized");
    return ESP_OK;
}

esp_err_t vizkey_profiles_map_event(const vizkey_matrix_event_t *event, vizkey_action_t *out_action)
{
    if (event == NULL || out_action == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_action->type = VIZKEY_ACTION_KEYBOARD;
    out_action->pressed = event->pressed;
    out_action->data.keyboard.modifiers = 0;

    // Temporary 1:1 mapping placeholder until real keymap/profile tables are loaded.
    const uint8_t matrix_index = (uint8_t)(event->row * 16u + event->col);
    out_action->data.keyboard.keycode = (uint8_t)(4u + (matrix_index % 40u));

    return ESP_OK;
}

esp_err_t vizkey_profiles_save_active(uint8_t profile_id)
{
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_ACTIVE_PROFILE, profile_id);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t vizkey_profiles_load_active(uint8_t *profile_id)
{
    if (profile_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(handle, NVS_KEY_ACTIVE_PROFILE, profile_id);
    nvs_close(handle);
    return err;
}
