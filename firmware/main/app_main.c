#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
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
static const uint32_t RUNTIME_POLL_INTERVAL_MS = 100;
static const char *CMD_SIM_ON = "matrix.sim.on";
static const char *CMD_SIM_OFF = "matrix.sim.off";
static const char *CMD_SIM_TOGGLE = "matrix.sim.toggle";
static const char *CMD_SIM_STATUS = "matrix.sim.status";
static const char *CMD_BLE_STANDBY = "ble.standby";
static const char *CMD_BLE_RECONNECT = "ble.reconnect";
static const char *CMD_BLE_PAIR_OPEN = "ble.pair.open";
static const char *CMD_BLE_CONNECTED = "ble.connected";
static const char *CMD_BLE_STATUS = "ble.status";
static const char *CMD_WIFI_AP_ON = "wifi.ap.on";
static const char *CMD_WIFI_AP_OFF = "wifi.ap.off";
static const char *CMD_WIFI_AP_STATUS = "wifi.ap.status";
static const char *CMD_LED_GPIO_PREFIX = "led.gpio.";

#if VIZKEY_DEBUG_LED_ENABLED
typedef enum {
    VIZKEY_DEBUG_LED_MODE_OFF = 0,
    VIZKEY_DEBUG_LED_MODE_STANDBY,
    VIZKEY_DEBUG_LED_MODE_BLE_RECONNECT,
    VIZKEY_DEBUG_LED_MODE_BLE_PAIRING,
    VIZKEY_DEBUG_LED_MODE_BLE_CONNECTED,
    VIZKEY_DEBUG_LED_MODE_FATAL,
} vizkey_debug_led_mode_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} vizkey_debug_led_rgb_t;

static const uint32_t DEBUG_LED_TASK_INTERVAL_MS = 20;
static const uint32_t DEBUG_LED_STANDBY_PERIOD_MS = 4000;
static const uint32_t DEBUG_LED_STANDBY_ON_MS = 60;
static const uint32_t DEBUG_LED_RECONNECT_PERIOD_MS = 1200;
static const uint32_t DEBUG_LED_RECONNECT_ON_MS = 80;
static const uint32_t DEBUG_LED_RECONNECT_GAP_MS = 160;
static const uint32_t DEBUG_LED_PAIRING_PERIOD_MS = 1000;
static const uint32_t DEBUG_LED_PAIRING_ON_MS = 180;
static const uint32_t DEBUG_LED_ACTIVITY_PULSE_MS = 35;
static const uint32_t DEBUG_LED_ACTIVITY_RATE_LIMIT_MS = 90;
static const uint32_t DEBUG_LED_BOOT_PULSE_MS = 120;
static const uint8_t DEBUG_LED_DIM_LEVEL = VIZKEY_LED_ACTIVE_LEVEL;
static const uint8_t DEBUG_LED_STANDBY_LEVEL = VIZKEY_LED_STANDBY_LEVEL;

static portMUX_TYPE s_debug_led_lock = portMUX_INITIALIZER_UNLOCKED;
static vizkey_debug_led_mode_t s_debug_led_mode = VIZKEY_DEBUG_LED_MODE_OFF;
static uint64_t s_debug_led_overlay_until_ms;
static uint32_t s_debug_led_last_activity_ms;

static uint32_t vizkey_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static const char *vizkey_debug_led_mode_name(vizkey_debug_led_mode_t mode)
{
    switch (mode) {
    case VIZKEY_DEBUG_LED_MODE_OFF:
        return "off";
    case VIZKEY_DEBUG_LED_MODE_STANDBY:
        return "standby";
    case VIZKEY_DEBUG_LED_MODE_BLE_RECONNECT:
        return "ble_reconnect";
    case VIZKEY_DEBUG_LED_MODE_BLE_PAIRING:
        return "ble_pairing";
    case VIZKEY_DEBUG_LED_MODE_BLE_CONNECTED:
        return "ble_connected";
    case VIZKEY_DEBUG_LED_MODE_FATAL:
        return "fatal";
    default:
        return "unknown";
    }
}

static vizkey_debug_led_rgb_t vizkey_debug_led_base_rgb(vizkey_debug_led_mode_t mode, uint32_t now_ms)
{
    switch (mode) {
    case VIZKEY_DEBUG_LED_MODE_STANDBY: {
        const uint32_t phase = now_ms % DEBUG_LED_STANDBY_PERIOD_MS;
        const uint8_t amber_green = (uint8_t)((DEBUG_LED_STANDBY_LEVEL / 3U) + 1U);
        return (phase < DEBUG_LED_STANDBY_ON_MS)
                   ? (vizkey_debug_led_rgb_t){.red = DEBUG_LED_STANDBY_LEVEL, .green = amber_green}
                   : (vizkey_debug_led_rgb_t){0};
    }
    case VIZKEY_DEBUG_LED_MODE_BLE_RECONNECT: {
        const uint32_t phase = now_ms % DEBUG_LED_RECONNECT_PERIOD_MS;
        const bool on = (phase < DEBUG_LED_RECONNECT_ON_MS) ||
                        (phase >= DEBUG_LED_RECONNECT_GAP_MS &&
                         phase < (DEBUG_LED_RECONNECT_GAP_MS + DEBUG_LED_RECONNECT_ON_MS));
        return on ? (vizkey_debug_led_rgb_t){.green = DEBUG_LED_DIM_LEVEL, .blue = DEBUG_LED_DIM_LEVEL}
                  : (vizkey_debug_led_rgb_t){0};
    }
    case VIZKEY_DEBUG_LED_MODE_BLE_PAIRING: {
        const uint32_t phase = now_ms % DEBUG_LED_PAIRING_PERIOD_MS;
        return (phase < DEBUG_LED_PAIRING_ON_MS) ? (vizkey_debug_led_rgb_t){.blue = DEBUG_LED_DIM_LEVEL}
                                                     : (vizkey_debug_led_rgb_t){0};
    }
    case VIZKEY_DEBUG_LED_MODE_BLE_CONNECTED:
        return (vizkey_debug_led_rgb_t){.green = DEBUG_LED_DIM_LEVEL};
    case VIZKEY_DEBUG_LED_MODE_FATAL: {
        const uint32_t phase = now_ms % 900U;
        const bool on = (phase < 100U) || (phase >= 200U && phase < 300U) || (phase >= 400U && phase < 500U);
        return on ? (vizkey_debug_led_rgb_t){.red = VIZKEY_BACKLIGHT_MAX_LEVEL} : (vizkey_debug_led_rgb_t){0};
    }
    case VIZKEY_DEBUG_LED_MODE_OFF:
    default:
        return (vizkey_debug_led_rgb_t){0};
    }
}

static void vizkey_debug_led_set_mode(vizkey_debug_led_mode_t mode)
{
    vizkey_debug_led_mode_t prev = mode;
    bool changed = false;

    portENTER_CRITICAL(&s_debug_led_lock);
    if (s_debug_led_mode != mode) {
        prev = s_debug_led_mode;
        s_debug_led_mode = mode;
        changed = true;
    }
    portEXIT_CRITICAL(&s_debug_led_lock);

    if (changed) {
        ESP_LOGI(
            TAG,
            "Debug LED mode: %s -> %s",
            vizkey_debug_led_mode_name(prev),
            vizkey_debug_led_mode_name(mode));
    }
}

static void vizkey_debug_led_pulse(uint32_t duration_ms)
{
    const uint64_t until_ms = (uint64_t)vizkey_now_ms() + duration_ms;
    portENTER_CRITICAL(&s_debug_led_lock);
    if (until_ms > s_debug_led_overlay_until_ms) {
        s_debug_led_overlay_until_ms = until_ms;
    }
    portEXIT_CRITICAL(&s_debug_led_lock);
}

static void vizkey_debug_led_mark_matrix_event(void)
{
    const uint32_t now_ms = vizkey_now_ms();
    portENTER_CRITICAL(&s_debug_led_lock);
    if ((uint32_t)(now_ms - s_debug_led_last_activity_ms) >= DEBUG_LED_ACTIVITY_RATE_LIMIT_MS) {
        s_debug_led_last_activity_ms = now_ms;
        const uint64_t until_ms = (uint64_t)now_ms + DEBUG_LED_ACTIVITY_PULSE_MS;
        if (until_ms > s_debug_led_overlay_until_ms) {
            s_debug_led_overlay_until_ms = until_ms;
        }
    }
    portEXIT_CRITICAL(&s_debug_led_lock);
}

static void vizkey_debug_led_set_pairing(void)
{
    vizkey_debug_led_set_mode(VIZKEY_DEBUG_LED_MODE_BLE_PAIRING);
}

static void vizkey_debug_led_set_reconnect(void)
{
    vizkey_debug_led_set_mode(VIZKEY_DEBUG_LED_MODE_BLE_RECONNECT);
}

static void vizkey_debug_led_set_connected(void)
{
    vizkey_debug_led_set_mode(VIZKEY_DEBUG_LED_MODE_BLE_CONNECTED);
}

static void vizkey_debug_led_set_standby(void)
{
    vizkey_debug_led_set_mode(VIZKEY_DEBUG_LED_MODE_STANDBY);
}

static void vizkey_debug_led_set_fatal(void)
{
    vizkey_debug_led_set_mode(VIZKEY_DEBUG_LED_MODE_FATAL);
}

static void vizkey_debug_led_boot_pulse(void)
{
    vizkey_debug_led_pulse(DEBUG_LED_BOOT_PULSE_MS);
}

static void vizkey_debug_led_task(void *ctx)
{
    (void)ctx;

    while (true) {
        vizkey_debug_led_mode_t mode;
        uint64_t overlay_until_ms;
        const uint32_t now_ms = vizkey_now_ms();

        portENTER_CRITICAL(&s_debug_led_lock);
        mode = s_debug_led_mode;
        overlay_until_ms = s_debug_led_overlay_until_ms;
        portEXIT_CRITICAL(&s_debug_led_lock);

        vizkey_debug_led_rgb_t rgb = vizkey_debug_led_base_rgb(mode, now_ms);
        if ((uint64_t)now_ms < overlay_until_ms) {
            rgb.red = VIZKEY_BACKLIGHT_MAX_LEVEL;
            rgb.green = VIZKEY_BACKLIGHT_MAX_LEVEL;
            rgb.blue = VIZKEY_BACKLIGHT_MAX_LEVEL;
        }

        (void)vizkey_backlight_set_rgb(rgb.red, rgb.green, rgb.blue);
        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_TASK_INTERVAL_MS));
    }
}

static esp_err_t vizkey_debug_led_start(void)
{
    portENTER_CRITICAL(&s_debug_led_lock);
    s_debug_led_mode = VIZKEY_DEBUG_LED_MODE_OFF;
    s_debug_led_overlay_until_ms = 0;
    s_debug_led_last_activity_ms = 0;
    portEXIT_CRITICAL(&s_debug_led_lock);

    return xTaskCreate(vizkey_debug_led_task, "vizkey_led", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK
                                                                                             : ESP_ERR_NO_MEM;
}
#else
static esp_err_t vizkey_debug_led_start(void)
{
    return ESP_OK;
}

static void vizkey_debug_led_mark_matrix_event(void)
{
}

static void vizkey_debug_led_set_pairing(void)
{
}

static void vizkey_debug_led_set_connected(void)
{
}

static void vizkey_debug_led_set_reconnect(void)
{
}

static void vizkey_debug_led_set_standby(void)
{
}

static void vizkey_debug_led_set_fatal(void)
{
}

static void vizkey_debug_led_boot_pulse(void)
{
}
#endif

static void vizkey_fatal_init(const char *step, esp_err_t err)
{
    ESP_LOGE(TAG, "Init failed at %s: %s", step, esp_err_to_name(err));
    vizkey_debug_led_set_fatal();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

#define VIZKEY_TRY_INIT(call)         \
    do {                              \
        esp_err_t _err = (call);      \
        if (_err != ESP_OK) {         \
            vizkey_fatal_init(#call, _err); \
            return;                   \
        }                             \
    } while (0)

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

typedef enum {
    VIZKEY_BLE_STATE_STANDBY = 0,
    VIZKEY_BLE_STATE_RECONNECT,
    VIZKEY_BLE_STATE_PAIRING,
    VIZKEY_BLE_STATE_CONNECTED,
} vizkey_ble_state_t;

static portMUX_TYPE s_ble_state_lock = portMUX_INITIALIZER_UNLOCKED;
static vizkey_ble_state_t s_ble_state = VIZKEY_BLE_STATE_STANDBY;
static uint64_t s_ble_state_deadline_ms;
static bool s_ble_transport_running;

static const char *vizkey_ble_state_name(vizkey_ble_state_t state)
{
    switch (state) {
    case VIZKEY_BLE_STATE_STANDBY:
        return "standby";
    case VIZKEY_BLE_STATE_RECONNECT:
        return "reconnect";
    case VIZKEY_BLE_STATE_PAIRING:
        return "pairing";
    case VIZKEY_BLE_STATE_CONNECTED:
        return "connected";
    default:
        return "unknown";
    }
}

static uint64_t vizkey_ble_deadline_for_state(vizkey_ble_state_t state, uint32_t now_ms)
{
    switch (state) {
    case VIZKEY_BLE_STATE_RECONNECT:
        return (VIZKEY_BLE_RECONNECT_WINDOW_MS > 0U) ? ((uint64_t)now_ms + VIZKEY_BLE_RECONNECT_WINDOW_MS) : 0U;
    case VIZKEY_BLE_STATE_PAIRING:
        return (VIZKEY_BLE_PAIRING_WINDOW_MS > 0U) ? ((uint64_t)now_ms + VIZKEY_BLE_PAIRING_WINDOW_MS) : 0U;
    case VIZKEY_BLE_STATE_STANDBY:
    case VIZKEY_BLE_STATE_CONNECTED:
    default:
        return 0U;
    }
}

static void vizkey_ble_get_status(vizkey_ble_state_t *out_state, uint64_t *out_deadline_ms, bool *out_transport_running)
{
    if (out_state == NULL || out_deadline_ms == NULL || out_transport_running == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_ble_state_lock);
    *out_state = s_ble_state;
    *out_deadline_ms = s_ble_state_deadline_ms;
    *out_transport_running = s_ble_transport_running;
    portEXIT_CRITICAL(&s_ble_state_lock);
}

static esp_err_t vizkey_ble_set_state(vizkey_ble_state_t next_state)
{
    bool transport_running;
    portENTER_CRITICAL(&s_ble_state_lock);
    transport_running = s_ble_transport_running;
    portEXIT_CRITICAL(&s_ble_state_lock);

    const bool needs_transport = (next_state == VIZKEY_BLE_STATE_RECONNECT) ||
                                 (next_state == VIZKEY_BLE_STATE_PAIRING) ||
                                 (next_state == VIZKEY_BLE_STATE_CONNECTED);

    if (needs_transport && !transport_running) {
        esp_err_t start_err = vizkey_hid_start();
        if (start_err != ESP_OK) {
            return start_err;
        }
        transport_running = true;
    } else if (!needs_transport && transport_running) {
        esp_err_t stop_err = vizkey_hid_stop();
        if (stop_err != ESP_OK) {
            return stop_err;
        }
        transport_running = false;
    }

    const uint32_t now_ms = vizkey_now_ms();
    const uint64_t deadline_ms = vizkey_ble_deadline_for_state(next_state, now_ms);

    switch (next_state) {
    case VIZKEY_BLE_STATE_STANDBY:
        vizkey_debug_led_set_standby();
        break;
    case VIZKEY_BLE_STATE_RECONNECT:
        vizkey_debug_led_set_reconnect();
        break;
    case VIZKEY_BLE_STATE_PAIRING:
        vizkey_debug_led_set_pairing();
        break;
    case VIZKEY_BLE_STATE_CONNECTED:
        vizkey_debug_led_set_connected();
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_ble_state_lock);
    s_ble_state = next_state;
    s_ble_state_deadline_ms = deadline_ms;
    s_ble_transport_running = transport_running;
    portEXIT_CRITICAL(&s_ble_state_lock);

    if (deadline_ms > (uint64_t)now_ms) {
        ESP_LOGI(
            TAG,
            "BLE state -> %s (timeout=%ums transport=%s)",
            vizkey_ble_state_name(next_state),
            (unsigned)(deadline_ms - (uint64_t)now_ms),
            transport_running ? "on" : "off");
    } else {
        ESP_LOGI(
            TAG,
            "BLE state -> %s (transport=%s)",
            vizkey_ble_state_name(next_state),
            transport_running ? "on" : "off");
    }
    return ESP_OK;
}

static void vizkey_ble_poll(void)
{
    vizkey_ble_state_t state;
    uint64_t deadline_ms;
    bool transport_running;
    vizkey_ble_get_status(&state, &deadline_ms, &transport_running);
    (void)transport_running;

    if ((state != VIZKEY_BLE_STATE_RECONNECT && state != VIZKEY_BLE_STATE_PAIRING) || deadline_ms == 0U) {
        return;
    }

    const uint64_t now_ms = vizkey_now_ms();
    if (now_ms < deadline_ms) {
        return;
    }

    ESP_LOGI(TAG, "BLE %s window expired; entering standby", vizkey_ble_state_name(state));
    if (vizkey_ble_set_state(VIZKEY_BLE_STATE_STANDBY) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to transition BLE state to standby after timeout");
    }
}

#if CONFIG_ESP_WIFI_ENABLED
static portMUX_TYPE s_wifi_ap_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *s_wifi_ap_netif;
static bool s_wifi_initialized;
static bool s_wifi_ap_running;
static uint64_t s_wifi_ap_deadline_ms;
#endif

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

static esp_err_t vizkey_start_wifi_ap(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    if (s_wifi_ap_running) {
        if (VIZKEY_WIFI_AP_AUTO_OFF_MS > 0U) {
            const uint64_t now_ms = vizkey_now_ms();
            portENTER_CRITICAL(&s_wifi_ap_lock);
            s_wifi_ap_deadline_ms = now_ms + VIZKEY_WIFI_AP_AUTO_OFF_MS;
            portEXIT_CRITICAL(&s_wifi_ap_lock);
        }
        return ESP_OK;
    }

    if (!s_wifi_initialized) {
        if (s_wifi_ap_netif == NULL) {
            s_wifi_ap_netif = esp_netif_create_default_wifi_ap();
            if (s_wifi_ap_netif == NULL) {
                return ESP_FAIL;
            }
        }

        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t init_err = esp_wifi_init(&init_cfg);
        if (init_err != ESP_OK && init_err != ESP_ERR_INVALID_STATE) {
            return init_err;
        }

        esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (storage_err != ESP_OK) {
            return storage_err;
        }

        s_wifi_initialized = true;
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

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
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

    const uint64_t now_ms = vizkey_now_ms();
    portENTER_CRITICAL(&s_wifi_ap_lock);
    s_wifi_ap_running = true;
    s_wifi_ap_deadline_ms = (VIZKEY_WIFI_AP_AUTO_OFF_MS > 0U) ? (now_ms + VIZKEY_WIFI_AP_AUTO_OFF_MS) : 0U;
    portEXIT_CRITICAL(&s_wifi_ap_lock);

    if (VIZKEY_WIFI_AP_AUTO_OFF_MS > 0U) {
        ESP_LOGI(
            TAG,
            "Wi-Fi AP started: ssid=%s channel=%u max_conn=%u auth=%s auto_off=%ums",
            VIZKEY_WIFI_AP_SSID,
            (unsigned)ap_cfg.ap.channel,
            (unsigned)ap_cfg.ap.max_connection,
            has_password ? "wpa2" : "open",
            (unsigned)VIZKEY_WIFI_AP_AUTO_OFF_MS);
    } else {
        ESP_LOGI(
            TAG,
            "Wi-Fi AP started: ssid=%s channel=%u max_conn=%u auth=%s",
            VIZKEY_WIFI_AP_SSID,
            (unsigned)ap_cfg.ap.channel,
            (unsigned)ap_cfg.ap.max_connection,
            has_password ? "wpa2" : "open");
    }
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Wi-Fi support disabled in this build; local HTTP/WS may be unreachable");
    return ESP_OK;
#endif
}

static esp_err_t vizkey_stop_wifi_ap(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    if (!s_wifi_ap_running) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        return err;
    }

    portENTER_CRITICAL(&s_wifi_ap_lock);
    s_wifi_ap_running = false;
    s_wifi_ap_deadline_ms = 0U;
    portEXIT_CRITICAL(&s_wifi_ap_lock);

    ESP_LOGI(TAG, "Wi-Fi AP stopped");
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

static bool vizkey_wifi_ap_is_running(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    bool running;
    portENTER_CRITICAL(&s_wifi_ap_lock);
    running = s_wifi_ap_running;
    portEXIT_CRITICAL(&s_wifi_ap_lock);
    return running;
#else
    return false;
#endif
}

static uint32_t vizkey_wifi_ap_remaining_ms(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    uint64_t deadline_ms;
    bool running;
    portENTER_CRITICAL(&s_wifi_ap_lock);
    running = s_wifi_ap_running;
    deadline_ms = s_wifi_ap_deadline_ms;
    portEXIT_CRITICAL(&s_wifi_ap_lock);

    if (!running || deadline_ms == 0U) {
        return 0U;
    }

    const uint64_t now_ms = vizkey_now_ms();
    return (deadline_ms > now_ms) ? (uint32_t)(deadline_ms - now_ms) : 0U;
#else
    return 0U;
#endif
}

static void vizkey_wifi_ap_poll(void)
{
#if CONFIG_ESP_WIFI_ENABLED
    uint64_t deadline_ms;
    bool running;
    portENTER_CRITICAL(&s_wifi_ap_lock);
    running = s_wifi_ap_running;
    deadline_ms = s_wifi_ap_deadline_ms;
    portEXIT_CRITICAL(&s_wifi_ap_lock);

    if (!running || deadline_ms == 0U) {
        return;
    }

    const uint64_t now_ms = vizkey_now_ms();
    if (now_ms < deadline_ms) {
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi AP auto-off timer elapsed; entering low-power AP-off state");
    if (vizkey_stop_wifi_ap() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop Wi-Fi AP after auto-off timeout");
    }
#endif
}

static esp_err_t vizkey_init_wifi_ap(void)
{
    return vizkey_start_wifi_ap();
}

static void vizkey_runtime_task(void *ctx)
{
    (void)ctx;

    while (true) {
        vizkey_ble_poll();
        vizkey_wifi_ap_poll();
        vTaskDelay(pdMS_TO_TICKS(RUNTIME_POLL_INTERVAL_MS));
    }
}

static void vizkey_on_matrix_event(const vizkey_matrix_event_t *event, void *ctx)
{
    (void)ctx;

    vizkey_debug_led_mark_matrix_event();

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

static bool vizkey_ws_cmd_starts_with(const char *payload, size_t len, const char *prefix)
{
    const size_t prefix_len = strlen(prefix);
    return len > prefix_len && memcmp(payload, prefix, prefix_len) == 0;
}

static esp_err_t vizkey_parse_u8(const char *text, size_t len, uint8_t *out_value)
{
    if (text == NULL || out_value == NULL || len == 0 || len > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[4];
    memcpy(buf, text, len);
    buf[len] = '\0';

    char *end = NULL;
    unsigned long value = strtoul(buf, &end, 10);
    if (end == NULL || *end != '\0' || value > 255UL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_value = (uint8_t)value;
    return ESP_OK;
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
    if (vizkey_ws_cmd_equals(payload, len, CMD_BLE_STANDBY)) {
        return vizkey_ble_set_state(VIZKEY_BLE_STATE_STANDBY);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_BLE_RECONNECT)) {
        return vizkey_ble_set_state(VIZKEY_BLE_STATE_RECONNECT);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_BLE_PAIR_OPEN)) {
        return vizkey_ble_set_state(VIZKEY_BLE_STATE_PAIRING);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_BLE_CONNECTED)) {
        return vizkey_ble_set_state(VIZKEY_BLE_STATE_CONNECTED);
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_BLE_STATUS)) {
        vizkey_ble_state_t state;
        uint64_t deadline_ms;
        bool transport_running;
        vizkey_ble_get_status(&state, &deadline_ms, &transport_running);

        const uint64_t now_ms = vizkey_now_ms();
        if (deadline_ms > now_ms) {
            ESP_LOGI(
                TAG,
                "BLE status: state=%s transport=%s timeout_in=%ums",
                vizkey_ble_state_name(state),
                transport_running ? "on" : "off",
                (unsigned)(deadline_ms - now_ms));
        } else {
            ESP_LOGI(
                TAG,
                "BLE status: state=%s transport=%s timeout_in=none",
                vizkey_ble_state_name(state),
                transport_running ? "on" : "off");
        }
        return ESP_OK;
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_WIFI_AP_ON)) {
        return vizkey_start_wifi_ap();
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_WIFI_AP_OFF)) {
        return vizkey_stop_wifi_ap();
    }
    if (vizkey_ws_cmd_equals(payload, len, CMD_WIFI_AP_STATUS)) {
        const bool running = vizkey_wifi_ap_is_running();
        const uint32_t remaining_ms = vizkey_wifi_ap_remaining_ms();
        if (running && remaining_ms > 0U) {
            ESP_LOGI(TAG, "Wi-Fi AP status: running timeout_in=%ums", (unsigned)remaining_ms);
        } else if (running) {
            ESP_LOGI(TAG, "Wi-Fi AP status: running timeout_in=none");
        } else {
            ESP_LOGI(TAG, "Wi-Fi AP status: stopped");
        }
        return ESP_OK;
    }
    if (vizkey_ws_cmd_starts_with(payload, len, CMD_LED_GPIO_PREFIX)) {
        const size_t prefix_len = strlen(CMD_LED_GPIO_PREFIX);
        uint8_t gpio = 0;
        esp_err_t parse_err = vizkey_parse_u8(payload + prefix_len, len - prefix_len, &gpio);
        if (parse_err != ESP_OK) {
            ESP_LOGW(TAG, "Invalid LED GPIO command: %.*s", (int)len, payload);
            return parse_err;
        }

        esp_err_t init_err =
            vizkey_backlight_init(
                (int)gpio,
                VIZKEY_BACKLIGHT_MAX_LEVEL,
                VIZKEY_BACKLIGHT_OUTPUT_INVERT,
                VIZKEY_BACKLIGHT_DRIVER);
        if (init_err != ESP_OK) {
            ESP_LOGW(TAG, "LED GPIO override failed for GPIO %u: %s", (unsigned)gpio, esp_err_to_name(init_err));
            return init_err;
        }
        vizkey_debug_led_boot_pulse();
        ESP_LOGI(TAG, "Debug LED GPIO override applied: GPIO %u", (unsigned)gpio);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown WS command: %.*s", (int)len, payload);
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting %s", VIZKEY_DEVICE_NAME);

    VIZKEY_TRY_INIT(vizkey_init_nvs());
    VIZKEY_TRY_INIT(
        vizkey_backlight_init(
            VIZKEY_BACKLIGHT_GPIO,
            VIZKEY_BACKLIGHT_MAX_LEVEL,
            VIZKEY_BACKLIGHT_OUTPUT_INVERT,
            VIZKEY_BACKLIGHT_DRIVER));
    VIZKEY_TRY_INIT(vizkey_debug_led_start());
    VIZKEY_TRY_INIT(vizkey_init_network_stack());
    VIZKEY_TRY_INIT(vizkey_init_wifi_ap());
    VIZKEY_TRY_INIT(vizkey_profiles_init());

    VIZKEY_TRY_INIT(vizkey_hid_set_transport(vizkey_hid_ble_transport()));
    VIZKEY_TRY_INIT(vizkey_ble_set_state(VIZKEY_BLE_STATE_STANDBY));

    VIZKEY_TRY_INIT(vizkey_ir_init(VIZKEY_IR_TX_GPIO, VIZKEY_IR_RX_GPIO));
    VIZKEY_TRY_INIT(vizkey_web_set_ws_handler(vizkey_on_ws_command));
    VIZKEY_TRY_INIT(vizkey_web_start());

    VIZKEY_TRY_INIT(vizkey_matrix_init());
    VIZKEY_TRY_INIT(vizkey_matrix_set_callback(vizkey_on_matrix_event, NULL));
    if (xTaskCreate(vizkey_matrix_task, "vizkey_matrix", 4096, NULL, 5, NULL) != pdPASS) {
        vizkey_fatal_init("xTaskCreate(vizkey_matrix_task)", ESP_ERR_NO_MEM);
        return;
    }
    if (xTaskCreate(vizkey_runtime_task, "vizkey_runtime", 4096, NULL, 4, NULL) != pdPASS) {
        vizkey_fatal_init("xTaskCreate(vizkey_runtime_task)", ESP_ERR_NO_MEM);
        return;
    }

    ESP_LOGI(
        TAG,
        "VizKey scaffold initialized (WS commands: matrix.sim.on/off/toggle/status, "
        "ble.standby/reconnect/pair.open/connected/status, wifi.ap.on/off/status, "
        "led.gpio.<n>)");
}
