# VizKey Bring-Up Plan

This plan is designed to keep risk low while moving quickly.

## Core Rule

Run firmware and hardware work in parallel, but delay irreversible remote modifications until firmware fundamentals are proven on bench wiring.

## Track Split

- Firmware track: matrix events -> action mapping -> BLE HID reports.
- Hardware discovery track: pin map, power rails, backlight path, IR path, and matrix behavior.

## Runtime Power Policy (Current Firmware Target)

- `standby`: BLE transport stopped, low-power LED heartbeat only.
- `reconnect`: short BLE reconnect window (`20s`), then auto-fallback to standby.
- `pairing`: open pairing window (`60s`), then auto-fallback to standby.
- `connected`: BLE transport running, connected LED state.
- Wi-Fi AP: starts for setup, auto-stops after `5 min` by default to reduce idle battery drain.

Commands to control policy from WS:

- `ble.standby`
- `ble.reconnect`
- `ble.pair.open`
- `ble.connected`
- `ble.status`
- `wifi.ap.on`
- `wifi.ap.off`
- `wifi.ap.status`
- `led.gpio.<n>` (debug pin override only)

## Phase 0: Baseline (Done)

- [x] ESP-IDF scaffold builds and boots.
- [x] Component boundaries are in place (matrix/profiles/hid/backlight/ir/web).
- [x] Device reaches `app_main()` without panic.

## Phase 1: Hardware Discovery (Start Now)

Goal: collect enough electrical truth to avoid guessing.

- [ ] Photograph both PCB sides and connector footprints.
- [ ] Label every accessible pad/connector pin in a pin map.
- [ ] Identify battery input and regulator output rails.
- [ ] Measure rail voltages at idle and during keypress/backlight toggle.
- [ ] Find matrix row/column groups with continuity testing.
- [ ] Confirm whether diodes exist and what ghosting constraints apply.
- [ ] Trace backlight driver stage (direct GPIO vs transistor/FET).
- [ ] Trace IR LED driver stage and polarity.

Artifacts to add:

- `docs/teardown/pinmap-v1.md`
- `docs/teardown/power-notes-v1.md`
- `docs/teardown/matrix-notes-v1.md`

Gate H1 (hardware confidence):

- Row/column map is documented and repeatable.
- Required voltage/current envelope is known.
- Backlight and IR control points are identified.

## Phase 2: Matrix Bring-Up on Bench Wiring

Goal: make matrix events real before any permanent install.

- [ ] Replace matrix stub with `espressif/keyboard_button`.
- [ ] Emit UART logs for `row/col/pressed/released`.
- [ ] Verify all keys can be reached with stable debounce.
- [ ] Record ghosting/rollover behavior.

Gate M1 (matrix confidence):

- Deterministic key events for full key coverage.
- No crash/lockup under rapid key spam.

## Phase 3: BLE HID Bring-Up

Goal: functional HID path to host.

- [ ] Replace BLE HID stubs with real Bluedroid HID init/callback path.
- [ ] Pair with at least one host (macOS/Windows/Linux).
- [ ] Send keyboard reports from matrix events.
- [ ] Send at least one consumer/media usage report.

Gate B1 (BLE confidence):

- Host pairing is stable across reboot.
- Key and media events arrive correctly.

## Phase 4: Device Features

- [ ] Profile ID in NVS.
- [ ] Macro/profile blob storage in LittleFS.
- [ ] Backlight PWM control path verified.
- [ ] Basic IR TX path verified.
- [ ] HTTP `/health` + one config endpoint + WS command path.

Gate F1 (feature confidence):

- Device can reboot and retain settings.
- No boot crash with BLE + web + storage enabled together.

## Phase 5: Remote Transplant (Irreversible Work)

Only start this after **H1 + M1 + B1** are complete.

- [ ] Build minimal adapter wiring harness or test interposer.
- [ ] Validate power first, then matrix, then backlight, then IR.
- [ ] Keep UART/USB debug access available during all tests.
- [ ] Run soak test (30-60 min mixed key activity).

Gate T1 (transplant confidence):

- Full key coverage works in real shell.
- Backlight and IR still function.
- No thermal, brownout, or reboot issues during soak.

## Immediate Next Actions

1. Execute Phase 1 measurements and document `pinmap-v1`.
2. Implement `keyboard_button` matrix integration and validate M1.
3. Implement real Bluedroid HID datapath and validate B1.
