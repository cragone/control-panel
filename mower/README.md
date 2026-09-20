# Mower — ESP32 Firmware

Autonomous lawn mower brain. Built with PlatformIO and the Arduino framework.

## Current behavior

Boots up, keeps blade and drive motors off until safety pins are configured,
then mows forward. On obstacle detection (ultrasonic < 35cm) it backs up,
turns, and resumes. Pressing the e-stop button latches motors and blade off
until the board is power-cycled. Battery voltage is sampled every 2s and
latches everything off below 11V.

It also estimates its own position every 5s by trilaterating WiFi signal
strength against fixed beacon boards (see `docs/localization.md` and
`../beacon/`) — logged over serial, not yet used for navigation.

Not yet implemented: using the position estimate for coverage (currently
pure reactive obstacle-avoidance, no memory of where it's been), MQTT
remote monitoring (see `../hardware` for the pattern used elsewhere in this
repo — the plan is to follow it here once the base loop is solid).

## Wiring (defaults, edit pins in `src/main.cpp` to match your build)

| ESP32 Pin | Connection                    |
|-----------|--------------------------------|
| GPIO 25   | Left motor driver IN1         |
| GPIO 26   | Left motor driver IN2         |
| GPIO 14   | Left motor driver ENA (PWM)   |
| GPIO 32   | Right motor driver IN1        |
| GPIO 33   | Right motor driver IN2        |
| GPIO 13   | Right motor driver ENB (PWM)  |
| GPIO 27   | Blade relay signal            |
| GPIO 5    | Ultrasonic TRIG                |
| GPIO 18   | Ultrasonic ECHO                |
| GPIO 34   | E-stop button (active LOW, needs external pull-up) |
| GPIO 35   | Battery voltage divider (100k/27k from battery+) |

Assumes a dual H-bridge driver (L298N or similar) for the two drive motors,
and a separate relay for the blade motor — never power the blade off the
same driver as the drive motors.

## Upload & Run

```bash
cd mower

# Build only
pio run

# Build and upload (auto-detects the port)
pio run --target upload

# Open serial monitor after uploading
pio device monitor --baud 115200
```

If the port isn't auto-detected, pass it explicitly with
`--upload-port /dev/ttyUSB0` (Linux/macOS) or `--upload-port COM3` (Windows).

## Safety notes

- The e-stop interrupt latches — once pressed, motors and blade stay off
  until the board is reset. It does not auto-clear.
- Motors and blade default to OFF at boot, before WiFi or any other
  subsystem is touched.
- Drive and blade power should be on physically separate circuits so a
  driver fault on one can't spin the other.
