# Localization — WiFi RSSI Trilateration

How the mower knows roughly where it is, using fixed WiFi beacons instead of
GPS. See `../beacon/` for the beacon firmware and `../src/main.cpp` for the
trilateration code (`BEACONS[]`, `trilaterate()`, `pollBeaconScan()`).

## How it works

1. Three (or more) ESP32 boards, each running `mower/beacon`, sit at fixed,
   known spots around the yard and broadcast a WiFi AP named
   `MOWER-BEACON-<id>` — no data connection, just a named signal to scan for.
2. Every 5s the mower does an async WiFi scan, reads the RSSI (signal
   strength) of each beacon it sees, and converts RSSI to an estimated
   distance using a log-distance path-loss model.
3. With 3+ distance estimates and known beacon positions, it solves for its
   own (x, y) in meters — same principle as GPS trilateration, just with
   WiFi signal strength standing in for satellite time-of-flight.

## Accuracy — set expectations correctly

RSSI-based distance is noisy: multipath, obstructions, even weather shift
the reading. Expect **1-3 meter error** in a small, open, fenced yard —
worse if the fence line has dense hedges or the beacons are close together.
This is fine for "roughly where am I in the yard," not for anything needing
tight precision. If your yard has flower beds or other things to avoid
precisely, don't rely on this alone.

## Setup

### 1. Pick an origin and place beacons

Pick one corner of the yard as `(0, 0)`. Place 3+ beacons at points that are
**not collinear** — a line of 3 beacons along one fence gives a degenerate,
unsolvable geometry. Corners or spread-out points work best; more spread
between beacons generally means better accuracy.

Measure each beacon's actual (x, y) position in meters from the origin with
a tape measure. Precision here matters — this is the ground truth the whole
system is built on.

### 2. Flash the beacons

```bash
cd mower/beacon
# set BEACON_ID to 1 in src/main.cpp, then:
pio run --target upload
# repeat with BEACON_ID = 2, 3, ... for each additional board
```

### 3. Enter beacon positions in the rover firmware

Edit `BEACONS[]` in `mower/src/main.cpp`:

```cpp
static const Beacon BEACONS[] = {
  { "MOWER-BEACON-1", 0.0f,  0.0f  },
  { "MOWER-BEACON-2", 10.0f, 0.0f  },
  { "MOWER-BEACON-3", 0.0f,  10.0f },
};
```

Replace the coordinates with your measured values (meters).

### 4. Calibrate `TX_POWER_AT_1M`

The path-loss model needs to know what RSSI to expect at a known distance.
Place the mower exactly 1 meter from a beacon, power it on, and watch the
serial log — it'll print `Position: ...` lines, but before you have 3
beacons calibrated you can log raw RSSI instead: temporarily add
`Serial.println(WiFi.RSSI(i))` in the scan loop, or just watch a phone WiFi
analyzer app next to the mower. Set `TX_POWER_AT_1M` to that measured value
(it's a negative dBm number, e.g. `-40`).

### 5. Tune `PATH_LOSS_EXPONENT`

Start at `2.5` (open outdoor space). If measured positions consistently
read farther than reality, lower it toward `2.0`; if closer than reality,
raise it toward `3.5-4.0` (more obstructions/attenuation).

## Current state

The firmware logs position estimates over serial but doesn't yet use them
for navigation — the mow/avoid state machine is still purely reactive
(ultrasonic-based). Position tracking is the foundation for smarter
coverage (e.g. "don't re-cross ground I've already mowed") but that logic
isn't built yet.
