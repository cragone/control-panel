# Hardware — ESP32 Firmware

Controls a light connected to GPIO pin 26 on an ESP32. Built with PlatformIO and the Arduino framework.

## Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode), **or** the [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
- USB cable (data-capable, not charge-only)
- ESP32 dev board

## Wiring

| ESP32 Pin | Connection     |
|-----------|----------------|
| GPIO 26   | Light / relay  |
| GND       | Ground         |

## Upload & Run

### Using VS Code + PlatformIO Extension

1. Open the `hardware/` folder in VS Code.
2. Connect the ESP32 via USB.
3. Click the **Upload** arrow (→) in the PlatformIO toolbar at the bottom of the window.
4. PlatformIO will compile and flash automatically. Watch the terminal for `Uploading .pio/build/esp32dev/firmware.bin`.
5. Once flashing completes, open the **Serial Monitor** (plug icon) at baud rate **115200** — you should see `Light on`.

### Using PlatformIO CLI

```bash
cd hardware

# Build only
pio run

# Build and upload (auto-detects the port)
pio run --target upload

# Open serial monitor after uploading
pio device monitor --baud 115200
```

If the port is not detected automatically, find it manually and pass it explicitly:

```bash
# Windows — look for COMx in Device Manager
pio run --target upload --upload-port COM3

# macOS / Linux
pio run --target upload --upload-port /dev/ttyUSB0
```

## Expected Behavior

On boot the ESP32 sets GPIO 26 HIGH (light on) and prints `Light on` over serial at 115200 baud. The `loop()` is empty, so the pin stays HIGH indefinitely until the board is reset or power-cycled.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Upload fails / port not found | Try a different USB cable; check Device Manager (Windows) or `ls /dev/tty*` (Linux/macOS) |
| `esptool` permission denied (Linux) | `sudo usermod -aG dialout $USER` then log out and back in |
| Board not detected on macOS | Install the CP210x or CH340 driver for your ESP32's USB chip |
| Serial monitor shows garbage | Make sure baud rate is set to **115200** |
