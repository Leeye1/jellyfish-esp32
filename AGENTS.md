# AGENTS.md — Jellyfish (ESP32-S3 aquarium monitoring)

## Prerequisites

- **ESP-IDF v5.5.3** at `C:\esp\v5.5.3\esp-idf`. All `idf.py` commands require the IDF environment activated first:
  ```powershell
  C:\esp\v5.5.3\esp-idf\export.ps1
  ```
- Target chip: **ESP32-S3-N16R8**. Flash port: **COM12**.

## Build & flash

```bash
idf.py set-target esp32s3    # one-time
idf.py build
idf.py flash monitor
```

There is no CI, no linter, and no test framework.

## Architecture

7 custom ESP-IDF components under `components/`:

| Component   | Role |
|-------------|------|
| `hw_config` | GPIO pin definitions, `gpio_config_init()` |
| `sensors`   | Sensor read (temp/pH/DO/salinity), threshold helpers |
| `rs485`     | UART1 Modbus RTU for DO & salinity sensors (9600 bps) |
| `actuator`  | Pump/heater/O2 direct GPIO control + smart hysteresis interfaces |
| `buzzer`    | Buzzer on/off |
| `rgb_led`   | WS2812 30-LED strip (smooth color transitions) |
| `network`   | WiFi STA + MQTT client |

5 FreeRTOS tasks created in `main/main.c:app_main`: `task_sensor_read`, `task_control_logic`, `task_network_publish`, `task_button_handler`, `task_led_effect`.

One managed dependency: `espressif/led_strip@^3.0.3` (declared in `main/idf_component.yml`, locked in `dependencies.lock`). After changing dependencies run `idf.py reconfigure`.

## Critical gotchas

- **Control logic is disabled.** The state machine in `main.c:108-131` that activates pumps/heater/buzzer based on sensor thresholds is **commented out**. The system currently only logs state changes; only the manual button (GPIO21) controls pumps. The docs in `.instructions.md` describe the intended behavior, not the actual running code.
- **Sensors use mock random data.** `sensors_add_random_offset()` generates values around fixed references — this is a test/demo harness, not real hardware readings.
- **Stale VS Code path.** `.vscode/settings.json` line 16 has `compile-commands-dir` pointing to `jellyfish\build` instead of `jellyfish_test_all\build`. Rebuild after moving the project to fix clangd IntelliSense.
- **sdkconfig is gitignored** — it is auto-generated. If the build fails with missing config, run `idf.py set-target esp32s3` first, then `idf.py reconfigure`.
- **Hardcoded credentials** in `components/network/network.c`: WiFi SSID `"Tim"`, MQTT broker `172.20.10.6:1883`, topic `sensor/water`.
- **All documentation is in Chinese** (`.instructions.md`, `control_logic.md`, `System Implementation.md`, `Test.md`, `sensor_reference.md`).

## Testing

No automated tests. `Test.md` (Chinese) defines a manual test plan with 4 smoke scenarios and a 24h stability test. Run `idf.py flash monitor` and observe serial logs tagged `jellyfish`.
