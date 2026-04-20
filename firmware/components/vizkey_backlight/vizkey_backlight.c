#include "vizkey_backlight.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "vizkey_backlight";

static bool s_initialized;
static uint8_t s_max_level;

esp_err_t vizkey_backlight_init(int gpio_num, uint8_t max_level)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer setup failed");

    const ledc_channel_config_t chan_cfg = {
        .gpio_num = gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "ledc channel setup failed");

    s_max_level = max_level;
    s_initialized = true;
    ESP_LOGI(TAG, "Backlight initialized on GPIO %d", gpio_num);
    return ESP_OK;
}

esp_err_t vizkey_backlight_set_level(uint8_t level)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t clamped = (level > s_max_level) ? s_max_level : level;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, clamped), TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), TAG, "update duty failed");
    return ESP_OK;
}

esp_err_t vizkey_backlight_set_enabled(bool enabled)
{
    return vizkey_backlight_set_level(enabled ? s_max_level : 0);
}
