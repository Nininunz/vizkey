# VizKey Firmware (ESP-IDF)

This directory is the ESP-IDF project root for VizKey.

## Stack

- Target: ESP32-S3
- Framework: ESP-IDF 5.x
- Matrix: `espressif/keyboard_button`
- HID seed: Bluedroid BLE HID device example structure
- Storage: NVS + LittleFS
- Backlight: LEDC
- IR: RMT (scaffolded)
- Web API: `esp_http_server` + WebSocket endpoint
- OTA: `esp_https_ota` (planned integration)

## Build

```bash
cd firmware
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

## Layout

- `main/`: boot flow and top-level orchestration (`app_main.c`)
- `components/vizkey_matrix`: raw matrix event production
- `components/vizkey_profiles`: profile/action mapping and storage
- `components/vizkey_hid`: action-to-report conversion and HID transport abstraction
- `components/vizkey_backlight`: LEDC backlight control
- `components/vizkey_ir`: IR TX/RX scaffolding
- `components/vizkey_web`: HTTP + WebSocket local config API
