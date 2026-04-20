# VizKey - Repurposing the Vizio XRT500

> [!IMPORTANT]
> **Concept project:** This is a proposed build, not a completed implementation.  
> Hardware integration and firmware are still in planning.

VizKey is a concept to repurpose the Vizio XRT500 (and likely XRT300) into a programmable BLE HID device by replacing the stock MCU with an ESP32-class microcontroller.

These remotes are compact, dual-sided, backlit, and genuinely usable compared to most cheap mini-keyboards. The goal is to preserve the existing hardware and replace the control logic with firmware that is open, configurable, and device-agnostic.

## Project Summary

VizKey proposes replacing the original MCU in an XRT500 with an ESP32 to expose the remote as a programmable BLE HID keyboard. The plan is to keep the membrane matrix, backlight, and IR blaster intact while swapping control logic.

If implemented, the device should:
- Pair as a BLE HID keyboard/media input device.
- Support remapping, layers/profiles, and macros.
- Store profiles on-device.
- Offer zero-install configuration from a browser (Web BLE and/or local AP + web UI).

## Goals

- Replace the stock MCU while preserving the keyboard matrix, backlight control, and IR blaster path.
- Implement a BLE HID keyboard with key remapping, layers/profiles, macros, and per-profile settings.
- Store configuration on-device with persistent flash storage.
- Provide a browser-based configurator with no native drivers.
- Preserve IR functionality and optionally map IR actions to BLE macros.

## Why This Device

- Compact full QWERTY layout with dedicated media keys.
- Backlit keys for low-light use.
- Dual-sided physical design provides high input density in a handheld form factor.
- Common and relatively inexpensive on the used market.
- Better key feel and build quality than most low-cost HTPC remotes.

Reference:
- [Vizio XRT500 Remote Information](https://support.vizio.com/s/article/XRT500-Remote-Information?language=en_US)

## Hardware Plan

- Donor device: Vizio XRT500 (XRT300 may be similar).
- Replacement MCU target: **ESP32-S3** (preferred for BLE now and optional USB HID later).
- Intended reused components: membrane key matrix, backlight LEDs/control path, IR LED and associated driver path, and battery/contact assembly.
- Likely additions: programming/debug header plus a breakout adapter or custom PCB for matrix/peripheral routing.

Power rail and voltage details are pending teardown measurements.

Teardown reference:
- [Vizio XRT500 teardown video](https://www.youtube.com/watch?v=uhpyRSl8vug)

## Firmware and Software Plan

- ESP-IDF 5.x firmware with componentized architecture.
- BLE HID implementation seeded from Espressif's Bluedroid HID device example.
- Matrix scan engine via `espressif/keyboard_button`.
- Backlight controller via LEDC.
- IR TX/RX integration via RMT.
- Profile storage split between NVS + LittleFS.
- Configurator web app over Web BLE where supported.
- Fallback local AP + hosted web UI path.
- OTA updates via `esp_https_ota`.

## Planned Features

- Any-key remapping and layer switching.
- Macros (key sequences, delays, media/system controls).
- Per-profile backlight settings.
- IR passthrough and programmable IR-triggered BLE macros.
- Multiple user profiles stored on-device.

## Status

Current state: **initial ESP-IDF scaffold in progress**.

Not yet done:
- Matrix pinout reverse engineering.
- Full BLE HID datapath from matrix events to on-air reports.
- Hardware adapter/PCB.
- Configurator implementation.

## Roadmap

1. Reverse-engineer the XRT500 matrix and connector pinout.
2. Build a breakout or custom PCB for ESP32 integration.
3. Complete firmware prototype: matrix scan + BLE HID basics.
4. Add profile storage + minimal configurator.
5. Iterate on power behavior, backlight UX, and IR integration.

## Repository Layout

- `firmware/`: ESP-IDF project root (`main/`, `components/`, `sdkconfig.defaults`, `partitions.csv`).
- `configurator/`: Browser configuration UI prototype.
- `docs/teardown/`: Teardown notes, photos, pin maps, and measurements.

## Contributing

Contributions are welcome, especially for:
- Teardown photos and annotated pinouts.
- Matrix mapping and electrical characterization.
- ESP32 HID firmware architecture.
- Configurator UX and browser compatibility testing.

If you want to collaborate, open an issue with your findings and hardware revision details.

## License

MIT
