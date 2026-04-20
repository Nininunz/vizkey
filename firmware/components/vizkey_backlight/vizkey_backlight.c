#include "vizkey_backlight.h"

#include "driver/ledc.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "vizkey_backlight";

static const uint32_t WS2812_RMT_RESOLUTION_HZ = 10000000; // 10MHz
static const size_t WS2812_RMT_MEM_BLOCK_SYMBOLS = 64;
static const int WS2812_TX_WAIT_TIMEOUT_MS = 50;

static const rmt_symbol_word_t s_ws2812_zero = {
    .level0 = 1,
    .duration0 = (uint16_t)(0.3 * WS2812_RMT_RESOLUTION_HZ / 1000000),
    .level1 = 0,
    .duration1 = (uint16_t)(0.9 * WS2812_RMT_RESOLUTION_HZ / 1000000),
};

static const rmt_symbol_word_t s_ws2812_one = {
    .level0 = 1,
    .duration0 = (uint16_t)(0.9 * WS2812_RMT_RESOLUTION_HZ / 1000000),
    .level1 = 0,
    .duration1 = (uint16_t)(0.3 * WS2812_RMT_RESOLUTION_HZ / 1000000),
};

static const rmt_symbol_word_t s_ws2812_reset = {
    .level0 = 0,
    .duration0 = (uint16_t)(WS2812_RMT_RESOLUTION_HZ / 1000000 * 50 / 2),
    .level1 = 0,
    .duration1 = (uint16_t)(WS2812_RMT_RESOLUTION_HZ / 1000000 * 50 / 2),
};

static bool s_initialized;
static uint8_t s_max_level;
static bool s_output_invert;
static int s_gpio_num = -1;
static vizkey_backlight_driver_t s_driver = VIZKEY_BACKLIGHT_DRIVER_LEDC;

static uint8_t s_last_level = UINT8_MAX;
static bool s_ws2812_color_valid;
static uint8_t s_ws2812_last_red;
static uint8_t s_ws2812_last_green;
static uint8_t s_ws2812_last_blue;
static rmt_channel_handle_t s_ws2812_chan;
static rmt_encoder_handle_t s_ws2812_encoder;

static size_t vizkey_ws2812_encode_cb(
    const void *data,
    size_t data_size,
    size_t symbols_written,
    size_t symbols_free,
    rmt_symbol_word_t *symbols,
    bool *done,
    void *arg)
{
    (void)arg;
    if (symbols_free < 8) {
        return 0;
    }

    const size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;
    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            symbols[symbol_pos++] = (bytes[data_pos] & bitmask) ? s_ws2812_one : s_ws2812_zero;
        }
        return symbol_pos;
    }

    symbols[0] = s_ws2812_reset;
    *done = true;
    return 1;
}

static void vizkey_ws2812_cleanup(void)
{
    if (s_ws2812_chan != NULL) {
        (void)rmt_disable(s_ws2812_chan);
        (void)rmt_del_channel(s_ws2812_chan);
        s_ws2812_chan = NULL;
    }
    if (s_ws2812_encoder != NULL) {
        (void)rmt_del_encoder(s_ws2812_encoder);
        s_ws2812_encoder = NULL;
    }
}

static esp_err_t vizkey_ws2812_init(int gpio_num, bool output_invert)
{
    esp_err_t err;

    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = gpio_num,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RMT_RESOLUTION_HZ,
        .mem_block_symbols = WS2812_RMT_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = 2,
        .flags.invert_out = output_invert ? 1 : 0,
    };
    err = rmt_new_tx_channel(&tx_cfg, &s_ws2812_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt tx channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_simple_encoder_config_t enc_cfg = {
        .callback = vizkey_ws2812_encode_cb,
    };
    err = rmt_new_simple_encoder(&enc_cfg, &s_ws2812_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt encoder init failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    err = rmt_enable(s_ws2812_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt channel enable failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    return ESP_OK;

cleanup:
    vizkey_ws2812_cleanup();
    return err;
}

static void vizkey_ledc_stop(void)
{
    (void)ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

static uint8_t vizkey_clamp_level(uint8_t value)
{
    return (value > s_max_level) ? s_max_level : value;
}

static uint8_t vizkey_level_to_255(uint8_t level)
{
    if (s_max_level == 0U) {
        return 0U;
    }
    return (uint8_t)(((uint32_t)level * 255U) / s_max_level);
}

static esp_err_t vizkey_ws2812_write_rgb255(uint8_t red_255, uint8_t green_255, uint8_t blue_255)
{
    if (s_ws2812_color_valid && s_ws2812_last_red == red_255 && s_ws2812_last_green == green_255 &&
        s_ws2812_last_blue == blue_255) {
        return ESP_OK;
    }

    uint8_t pixel[3] = {green_255, red_255, blue_255};
    const rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };
    ESP_RETURN_ON_ERROR(
        rmt_transmit(s_ws2812_chan, s_ws2812_encoder, pixel, sizeof(pixel), &tx_cfg),
        TAG,
        "ws2812 transmit failed");
    ESP_RETURN_ON_ERROR(
        rmt_tx_wait_all_done(s_ws2812_chan, WS2812_TX_WAIT_TIMEOUT_MS),
        TAG,
        "ws2812 wait failed");

    s_ws2812_last_red = red_255;
    s_ws2812_last_green = green_255;
    s_ws2812_last_blue = blue_255;
    s_ws2812_color_valid = true;
    return ESP_OK;
}

esp_err_t vizkey_backlight_init(
    int gpio_num,
    uint8_t max_level,
    bool output_invert,
    vizkey_backlight_driver_t driver)
{
    if (s_initialized && s_gpio_num == gpio_num && s_output_invert == output_invert && s_driver == driver) {
        s_max_level = max_level;
        return ESP_OK;
    }

    if (s_initialized) {
        if (s_driver == VIZKEY_BACKLIGHT_DRIVER_WS2812) {
            vizkey_ws2812_cleanup();
        } else {
            vizkey_ledc_stop();
        }
    }

    if (driver == VIZKEY_BACKLIGHT_DRIVER_WS2812) {
        ESP_RETURN_ON_ERROR(vizkey_ws2812_init(gpio_num, output_invert), TAG, "ws2812 init failed");
    } else {
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
            .flags.output_invert = output_invert ? 1 : 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&chan_cfg), TAG, "ledc channel setup failed");
    }

    s_gpio_num = gpio_num;
    s_max_level = max_level;
    s_output_invert = output_invert;
    s_driver = driver;
    s_last_level = UINT8_MAX;
    s_ws2812_color_valid = false;
    s_initialized = true;
    ESP_LOGI(
        TAG,
        "Backlight initialized on GPIO %d (driver=%s invert=%s)",
        gpio_num,
        driver == VIZKEY_BACKLIGHT_DRIVER_WS2812 ? "ws2812" : "ledc",
        s_output_invert ? "true" : "false");
    return ESP_OK;
}

esp_err_t vizkey_backlight_set_level(uint8_t level)
{
    return vizkey_backlight_set_rgb(level, level, level);
}

esp_err_t vizkey_backlight_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t red_clamped = vizkey_clamp_level(red);
    const uint8_t green_clamped = vizkey_clamp_level(green);
    const uint8_t blue_clamped = vizkey_clamp_level(blue);

    if (s_driver == VIZKEY_BACKLIGHT_DRIVER_WS2812) {
        return vizkey_ws2812_write_rgb255(
            vizkey_level_to_255(red_clamped),
            vizkey_level_to_255(green_clamped),
            vizkey_level_to_255(blue_clamped));
    }

    uint8_t white_level = red_clamped;
    if (green_clamped > white_level) {
        white_level = green_clamped;
    }
    if (blue_clamped > white_level) {
        white_level = blue_clamped;
    }
    if (white_level == s_last_level) {
        return ESP_OK;
    }
    s_last_level = white_level;

    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, white_level), TAG, "set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0), TAG, "update duty failed");
    return ESP_OK;
}

esp_err_t vizkey_backlight_set_enabled(bool enabled)
{
    return vizkey_backlight_set_level(enabled ? s_max_level : 0);
}
