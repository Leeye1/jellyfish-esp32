# Jellyfish — ESP32-S3 Aquarium Monitoring System

ESP32-S3-N16R8 based automated monitoring and control system for tropical jellyfish aquariums. Reads temperature, pH, dissolved oxygen, and salinity via RS485 Modbus RTU and ADC sensors; controls pumps, heater, and O2 pump with hysteresis logic; publishes telemetry via MQTT.

---

## Project Structure

| Path | Description |
|------|-------------|
| `main/main.c` | System entry point: 5 FreeRTOS tasks, state machine, startup sequence |
| `main/idf_component.yml` | Managed dependency: `espressif/led_strip@^3.0.3` |
| `components/hw_config/` | GPIO pin definitions table and `gpio_config_init()` |
| `components/sensors/` | Sensor data acquisition: pH via ADC, DO/salinity/temp via RS485 Modbus RTU. Threshold helpers and `sensors_read_all()` |
| `components/rs485/` | Modbus RTU protocol stack (UART1, 9600bps, CRC16, frame alignment, DO & salinity sensor commands) |
| `components/actuator/` | Actuator GPIO control + smart hysteresis interfaces (O2 pump, heater, water pumps, circulation pump PWM) |
| `components/buzzer/` | GPIO10 buzzer on/off control |
| `components/rgb_led/` | WS2812 30-LED strip: 60-second 6-color rainbow gradient with smooth interpolation |
| `components/network/` | WiFi STA + MQTT client (JSON telemetry publishing) |
| `doc/jellyfish_code_reference.md` | Full code reference: architecture, API, component explanations |

---

## Steps of Programming the Board

### 1. Install ESP-IDF Extension for VSCode

Install the official ESP-IDF extension from the VSCode marketplace:

- **Extension**: [ESP-IDF by Espressif Systems](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)
- **Setup guide**: [ESP-IDF VSCode Extension Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/vscode-setup.html)

After installation, configure the extension to use the existing ESP-IDF installation:

1. Press `Ctrl+Shift+P` → search "ESP-IDF: Configure ESP-IDF Extension"
2. Choose "Use Existing Setup"
3. Set the IDF path to `C:\esp\v5.5.3\esp-idf`
4. Let the extension verify the Python environment and toolchain

### 2. Environment Setup

The project requires **ESP-IDF v5.5.3** with the ESP32-S3 toolchain.

If using the command line instead of VSCode:

```powershell
# Activate the ESP-IDF environment
C:\esp\v5.5.3\esp-idf\export.ps1

# Verify the environment is active
idf.py --version
```

Before first build, ensure the Python virtual environment is installed:

```powershell
cd C:\esp\v5.5.3\esp-idf
.\\install.ps1
```

### 3. Hardware Connection

Connect your computer to the ESP32-S3 development board using a TTL-USB adapter or the board's built-in USB port.

**Using TTL-USB adapter**:

| TTL-USB | ESP32-S3 |
|---------|----------|
| TX | RX (GPIO44) |
| RX | TX (GPIO43) |
| GND | GND |

**Using the built-in USB-C port**:

If your ESP32-S3 board has a USB-C connector with an integrated USB-UART bridge (CP210x or CH340), simply connect via USB-C — no extra adapter needed. The flash port is typically **COM12** (check Device Manager under "Ports (COM & LPT)").

### 4. Using the ESP-IDF VSCode Extension

The extension adds a toolbar at the bottom of VSCode with the following buttons:

| Button | Name | Description |
|--------|------|-------------|
| 🎯 | **Set Target** | Select the target chip: choose `esp32s3` |
| ⚙️ | **Menuconfig** | Open `idf.py menuconfig` to edit sdkconfig (optional) |
| 🔨 | **Build** | Compile the project |
| ⚡ | **Flash** | Burn the firmware to the board |
| 👁 | **Monitor** | Open serial monitor to view logs |
| 🧹 | **Full Clean** | Delete build artifacts for a clean rebuild |

**Standard workflow — from code to board**:

1. **Set Target** → Select `esp32s3` *(one-time setup)*
2. **Build** (`🔨`) → Compile all components and generate the binary
3. **Flash** (`⚡`) → Select **UART** as the flash method, choose your COM port (e.g. `COM12`), then click Flash
4. **Monitor** (`👁`) → Watch serial output (baud rate: `115200`). Logs are tagged with `jellyfish` prefix.

**Key configuration defaults**:

| Setting | Value |
|---------|-------|
| Target chip | ESP32-S3 |
| Flash method | UART |
| Flash port | COM12 |
| Monitor baud | 115200 |

**Troubleshooting**:

- If the Build button is missing, verify the ESP-IDF extension is installed and configured
- If Flash fails, check: (a) the correct COM port is selected, (b) the board is in download mode (hold BOOT → press EN → release BOOT)
- After moving the project folder, rebuild with `idf.py set-target esp32s3` then `idf.py reconfigure` to regenerate sdkconfig

---

## Branches

| Branch | Mode | Sensor Data |
|--------|------|-------------|
| `master` | Production | Real RS485 + ADC readings |
| `demo` | Demonstration | Mock random-offset data (`DEMO_MODE` macro enabled) |

Both branches are functionally identical except for sensor data source — `demo` generates synthetic values so the system can be demonstrated without physical sensor hardware.

---

## Build & Flash (Command Line)

```powershell
# Activate ESP-IDF
C:\esp\v5.5.3\esp-idf\export.ps1

# Set target (first time only)
idf.py set-target esp32s3

# Build
idf.py build

# Flash & monitor
idf.py flash monitor
```

Dependency management: after changing `main/idf_component.yml`, run `idf.py reconfigure`.

---

## System Overview

- **5 FreeRTOS tasks**: sensor reading (5s cycle) → control logic (event-driven) → network publish (15s) + button handler (50ms poll) + LED animation (100ms)
- **Sensor types**: pH (ADC), dissolved oxygen & salinity & temperature (RS485 Modbus RTU)
- **Actuators**: 2x water pumps, O2 pump, heater, circulation pump (PWM)
- **Outputs**: WS2812 30-LED rainbow gradient, MQTT JSON telemetry, buzzer alarm
- **Control**: Hysteresis-based smart control with dead zones to prevent relay chattering
