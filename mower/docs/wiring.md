# Mower — Design & Wiring

Companion docs to `../src/main.cpp`. Two diagrams:

- [`wiring-diagram.svg`](./wiring-diagram.svg) — full electrical schematic
- [`chassis-layout.svg`](./chassis-layout.svg) — top-down mechanical layout

## Pinout

| ESP32 Pin | Connects to                          | Notes |
|-----------|----------------------------------------|-------|
| GPIO25    | L298N IN1 (left motor)                 | |
| GPIO26    | L298N IN2 (left motor)                 | |
| GPIO14    | L298N ENA (left motor, PWM)            | |
| GPIO32    | L298N IN3 (right motor)                | |
| GPIO33    | L298N IN4 (right motor)                | |
| GPIO13    | L298N ENB (right motor, PWM)           | avoid GPIO12 — it's a strapping pin |
| GPIO27    | Relay module IN (blade cutoff)         | |
| GPIO5     | HC-SR04 TRIG                           | |
| GPIO18    | HC-SR04 ECHO                           | |
| GPIO34    | E-STOP sense (NO contact + pull-up)    | input-only pin, no internal pull resistor — needs external 10k pull-up to 3.3V |

## Power chain

```
Battery(12V) → Main Switch → Fuse (20A) → E-STOP (NC contact) → +12V bus
                                                                     ├─ L298N VIN  → drive motors
                                                                     ├─ Relay COM  → blade motor
                                                                     └─ Buck converter → 5V rail → ESP32, relay VCC, HC-SR04 VCC, L298N logic
```

**Why the e-stop is wired on the NC contact, not just read as a GPIO:** a software-only e-stop dies with the ESP32 — if it crashes or hangs, the motors keep running. Wiring the mushroom switch's NC contact in series with the +12V bus means pressing it physically breaks motor and blade power regardless of firmware state. The switch's NO contact is wired separately to GPIO34 (through a pull-up) purely so the firmware can log the event and refuse to resume — see `onEstop()` / `estopLatched` in `main.cpp`.

All module grounds return to one common ground bus tied to battery negative. Don't let the blade motor's ground share a return path with the ESP32's logic ground right at the connector — tie them at the battery terminal instead, to keep blade motor noise off the signal lines.

## Mechanical layout

- Battery mounted low and toward the rear, over the drive axle — keeps center of gravity behind the blade and improves traction.
- Blade centered under the deck, motor isolated from the electronics bay.
- Caster wheel front-center for turning clearance; two driven wheels on the rear axle (differential steering, matches `turnInPlace()` in firmware).
- HC-SR04 mounted on the front bumper, facing the direction of travel.
- E-stop mounted on a rear mast/handle — reachable without leaning over the blade.

## Not yet in the SVGs

- Exact chassis dimensions/cut lines (footprint is illustrative)
- Battery box / IP rating for outdoor wet-grass exposure
- Blade guard and deck skirt for the ejection zone

## Bill of materials

| Part | Qty | Link |
|---|---|---|
| ESP32 dev board (2-pack) | 1 | [MELIFE ESP32-WROOM-32 2pk](https://www.amazon.com/MELIFE-Development-Dual-Mode-Microcontroller-Integrated/dp/B07Q576VWZ) |
| L298N motor driver | 1 | [Qunqi L298N](https://www.amazon.com/Qunqi-Controller-Module-Stepper-Arduino/dp/B014KMHSW6) |
| 12V gear motor + wheel | 2 | [DC 12V Encoder Gear Motor w/ wheel kit](https://www.amazon.com/clp/B07X5P1584) |
| HC-SR04 ultrasonic sensor (5-pack) | 1 | [ELEGOO HC-SR04 5pk](https://www.amazon.com/ELEGOO-HC-SR04-Ultrasonic-Distance-MEGA2560/dp/B01COSN7O6) |
| 775 blade motor + blade + bracket kit | 1 | [DIY Lawn Mower 775 Motor Set](https://www.amazon.com/12-24V-Manganese-Mounting-Bracket-Garden/dp/B09Y431FH8) |
| 5V relay module (2-pack) | 1 | [HiLetgo 2pc 5V relay](https://www.amazon.com/HiLetgo-Channel-optocoupler-Support-Trigger/dp/B00LW15A4W) |
| 12V 12Ah AGM/SLA battery | 1 | [ExpertPower EXP12120](https://www.amazon.com/ExpertPower-EXP12120-Volt-Rechargeable-battery/dp/B00A82A2ZS) |
| 12V SLA charger | 1 | [Zeglavi 12V 1.25A charger](https://www.amazon.com/Zeglavi-Battery-Charging-Motorcycles-Indicator/dp/B0CFZWLJPV) |
| 12V→5V buck converter | 1 | [DROK 5A buck converter](https://www.amazon.com/Converter-DROK-Regulator-Inverter-Transformer/dp/B01NALDSJ0) |
| Inline fuse holder + fuses | 1 | [Anyongora 4pk fuse holder kit](https://www.amazon.com/Anyongora-Inline-Waterproof-Automotive-Standard/dp/B0CL7MLY6T) |
| Main power toggle switch | 1 | [DaierTek 12V 50A IP65 toggle](https://www.amazon.com/DaierTek-Waterproof-Toggle-Aluminum-Housing/dp/B08S34VYX6) |
| E-stop mushroom button (1NO 1NC) | 1 | [Gebildet 12V mushroom e-stop](https://www.amazon.com/Gebildet-2pcs-Emergency-Button-Mushroom/dp/B0D86NMXYC) |
| 18AWG wire, red+black spool | 1 | [C-able 100ft 18AWG red/black](https://www.amazon.com/C-able-Electrical-Stranded-Voltage-Extension/dp/B01N0A16X2) |
| Ring terminal crimp kit | 1 | [225pc ring terminal assortment](https://www.amazon.com/Terminals-Electrical-Connectors-Assortment-Yellow/dp/B073NN7GWM) |
