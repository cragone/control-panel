# Beacon — WiFi Localization Fixed Node

Tiny firmware for fixed ESP32 boards mounted around the yard. Each one just
broadcasts a WiFi access point named `MOWER-BEACON-<id>` — the mower
(`../src/main.cpp`) scans for these and trilaterates its own position from
signal strength. See `../docs/localization.md` for the full setup.

## Flashing multiple beacons

You need at least 3 boards, each flashed with a different `BEACON_ID`:

```bash
cd mower/beacon
# edit BEACON_ID in src/main.cpp (1, then 2, then 3...) before each upload
pio run --target upload
```

## Mounting

- Power each beacon off USB power banks, or a small battery + buck
  converter like the mower's own 5V rail.
- Weatherproof the enclosure if mounted outdoors — a $5 IP65 project box is
  enough, it's just carrying a WiFi radio, no heat to dissipate.
- Mount at consistent height (fence-post height is convenient) and note
  each beacon's exact (x, y) position — you'll need it for
  `mower/src/main.cpp`'s `BEACONS[]` table.
