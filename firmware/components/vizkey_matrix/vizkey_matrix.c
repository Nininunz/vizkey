#include "vizkey_matrix.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "vizkey_matrix";

typedef struct {
    uint8_t row;
    uint8_t col;
} vizkey_matrix_sim_key_t;

static const vizkey_matrix_sim_key_t s_sim_keys[] = {
    {.row = 0, .col = 0},
    {.row = 0, .col = 1},
    {.row = 1, .col = 0},
    {.row = 1, .col = 1},
    {.row = 2, .col = 3},
    {.row = 3, .col = 2},
};

static const uint32_t SIM_START_DELAY_MS = 600;
static const uint32_t SIM_PRESS_HOLD_MS = 70;
static const uint32_t SIM_GAP_MS = 180;
static const uint32_t SIM_LOOP_PAUSE_MS = 900;

static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static vizkey_matrix_callback_t s_callback;
static void *s_callback_ctx;
static size_t s_sim_key_index;
static bool s_sim_release_next;
static bool s_simulation_enabled;
static uint64_t s_next_event_ms;

static uint32_t vizkey_matrix_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void vizkey_matrix_reset_simulator_locked(uint64_t now_ms)
{
    s_sim_key_index = 0;
    s_sim_release_next = false;
    s_next_event_ms = now_ms + SIM_START_DELAY_MS;
}

static void vizkey_matrix_dispatch_event(const vizkey_matrix_event_t *event)
{
    if (s_callback != NULL) {
        s_callback(event, s_callback_ctx);
    }
    ESP_LOGI(
        TAG,
        "matrix event row=%u col=%u %s",
        event->row,
        event->col,
        event->pressed ? "pressed" : "released");
}

esp_err_t vizkey_matrix_init(void)
{
    const uint64_t now_ms = vizkey_matrix_now_ms();
    portENTER_CRITICAL(&s_state_lock);
    s_simulation_enabled = false;
    vizkey_matrix_reset_simulator_locked(now_ms);
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(
        TAG,
        "Matrix scanner init (synthetic stream disabled; send matrix.sim.on over /ws to enable)");
    return ESP_OK;
}

esp_err_t vizkey_matrix_set_callback(vizkey_matrix_callback_t callback, void *ctx)
{
    s_callback = callback;
    s_callback_ctx = ctx;
    return ESP_OK;
}

esp_err_t vizkey_matrix_poll(void)
{
    if (s_callback == NULL) {
        return ESP_OK;
    }

    const uint64_t now_ms = vizkey_matrix_now_ms();
    vizkey_matrix_event_t event = {0};
    bool dispatch = false;

    portENTER_CRITICAL(&s_state_lock);
    if (!s_simulation_enabled || now_ms < s_next_event_ms) {
        portEXIT_CRITICAL(&s_state_lock);
        return ESP_OK;
    }

    const vizkey_matrix_sim_key_t key = s_sim_keys[s_sim_key_index];
    event = (vizkey_matrix_event_t){
        .row = key.row,
        .col = key.col,
        .pressed = !s_sim_release_next,
        .timestamp_ms = (uint32_t)now_ms,
    };
    dispatch = true;

    if (!s_sim_release_next) {
        s_sim_release_next = true;
        s_next_event_ms = now_ms + SIM_PRESS_HOLD_MS;
        portEXIT_CRITICAL(&s_state_lock);
    } else {
        s_sim_release_next = false;
        s_sim_key_index = (s_sim_key_index + 1U) % (sizeof(s_sim_keys) / sizeof(s_sim_keys[0]));
        s_next_event_ms = now_ms + SIM_GAP_MS;
        if (s_sim_key_index == 0U) {
            s_next_event_ms += SIM_LOOP_PAUSE_MS;
        }
        portEXIT_CRITICAL(&s_state_lock);
    }

    if (dispatch) {
        vizkey_matrix_dispatch_event(&event);
    }

    return ESP_OK;
}

esp_err_t vizkey_matrix_inject_event(const vizkey_matrix_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    vizkey_matrix_event_t copy = *event;
    if (copy.timestamp_ms == 0U) {
        copy.timestamp_ms = vizkey_matrix_now_ms();
    }

    vizkey_matrix_dispatch_event(&copy);
    return ESP_OK;
}

esp_err_t vizkey_matrix_set_simulation_enabled(bool enabled)
{
    bool changed = false;
    const uint64_t now_ms = vizkey_matrix_now_ms();

    portENTER_CRITICAL(&s_state_lock);
    if (s_simulation_enabled != enabled) {
        s_simulation_enabled = enabled;
        changed = true;
        if (enabled) {
            vizkey_matrix_reset_simulator_locked(now_ms);
        }
    }
    portEXIT_CRITICAL(&s_state_lock);

    if (changed) {
        ESP_LOGI(TAG, "Synthetic matrix stream %s", enabled ? "enabled" : "disabled");
    }
    return ESP_OK;
}

bool vizkey_matrix_is_simulation_enabled(void)
{
    bool enabled;
    portENTER_CRITICAL(&s_state_lock);
    enabled = s_simulation_enabled;
    portEXIT_CRITICAL(&s_state_lock);
    return enabled;
}
