#include <Arduino.h>
#include <WiFi.h>

// ---- Pins ---------------------------------------------------------------
// Drive motors via a dual H-bridge (e.g. L298N). ENA/ENB are PWM speed pins.
#define LEFT_IN1   25
#define LEFT_IN2   26
#define LEFT_EN    14   // PWM
#define RIGHT_IN1  32
#define RIGHT_IN2  33
#define RIGHT_EN   13   // PWM

// Blade motor, gated by its own relay — never share this with drive power.
#define BLADE_RELAY_PIN 27

// Ultrasonic obstacle sensor (HC-SR04 style).
#define TRIG_PIN 5
#define ECHO_PIN 18

// Physical e-stop button. GPIO34 is input-only with no internal pull-up,
// so the button circuit needs its own external pull-up and must pull the
// pin LOW when pressed.
#define ESTOP_PIN 34

// Battery voltage sense. GPIO35 is input-only (ADC1_CH7) — pairs naturally
// with GPIO34 for two no-cost sense lines. Battery+ feeds a divider (100k
// top / 27k bottom) down to this pin so a 15V worst-case charge voltage
// lands at ~3.2V, safely under the ESP32's 3.3V ADC limit.
#define BATTERY_ADC_PIN 35
#define BATTERY_DIVIDER_RATIO (27.0f / (100.0f + 27.0f))
#define ADC_REF_VOLTAGE 3.3f
#define ADC_MAX_COUNT   4095.0f

// ---- Tuning ---------------------------------------------------------------
#define DRIVE_SPEED       180   // 0-255
#define TURN_SPEED        160
#define OBSTACLE_CM       35    // stop/avoid distance
#define BACKUP_MS         600
#define TURN_MS           500
#define BATTERY_CHECK_MS      2000  // how often to sample battery voltage
#define LOW_BATTERY_VOLTAGE   11.0f // stop and latch below this (12V SLA — don't deep-discharge)

enum State { MOWING, BACKING_UP, TURNING, ESTOPPED, BATTERY_LOW };
static State state = MOWING;
static unsigned long stateStartedAt = 0;
static unsigned long lastBatteryCheckAt = 0;

// ---- Localization (WiFi RSSI trilateration) -------------------------------
// Fixed ESP32 boards (see ../beacon) each broadcast an AP named
// MOWER-BEACON-<n>. This board estimates its own (x, y) position in meters
// by trilaterating against 3+ of them at known, surveyed spots. Good to a
// couple meters outdoors — fine for a small fenced yard, not for anything
// needing tight precision. See ../docs/localization.md for setup/calibration.

struct Beacon { const char* ssid; float x, y; };

// Measure each beacon's position with a tape measure from one fixed corner
// of the yard (the origin, 0,0) and fill these in before relying on it.
static const Beacon BEACONS[] = {
  { "MOWER-BEACON-1", 0.0f,  0.0f  },
  { "MOWER-BEACON-2", 10.0f, 0.0f  },
  { "MOWER-BEACON-3", 0.0f,  10.0f },
};
#define BEACON_COUNT (sizeof(BEACONS) / sizeof(BEACONS[0]))

// Path-loss calibration — both drift with environment. Measure
// TX_POWER_AT_1M by placing the mower exactly 1m from a beacon and reading
// its logged RSSI. Start PATH_LOSS_EXPONENT at 2.5 for open yard, raise
// toward 3-4 if fences/obstructions shorten effective range.
#define TX_POWER_AT_1M      -40.0f
#define PATH_LOSS_EXPONENT   2.5f
#define LOCATE_INTERVAL_MS   5000

static bool wifiScanInProgress = false;
static unsigned long lastScanStartAt = 0;
static float mowerX = 0, mowerY = 0;
static bool havePositionFix = false;

float distanceFromRssi(int rssi) {
  return pow(10.0f, (TX_POWER_AT_1M - rssi) / (10.0f * PATH_LOSS_EXPONENT));
}

// Solves 2D trilateration from three (x, y, distance) points by linearizing
// against the first point. Returns false if the beacons are collinear.
bool trilaterate(float x1, float y1, float d1,
                  float x2, float y2, float d2,
                  float x3, float y3, float d3,
                  float &outX, float &outY) {
  float A = 2 * (x2 - x1), B = 2 * (y2 - y1);
  float C = d1 * d1 - d2 * d2 - x1 * x1 + x2 * x2 - y1 * y1 + y2 * y2;
  float D = 2 * (x3 - x1), E = 2 * (y3 - y1);
  float F = d1 * d1 - d3 * d3 - x1 * x1 + x3 * x3 - y1 * y1 + y3 * y3;

  float det = A * E - B * D;
  if (fabs(det) < 1e-6f) return false; // beacons are collinear

  outX = (C * E - B * F) / det;
  outY = (A * F - C * D) / det;
  return true;
}

void startBeaconScan() {
  WiFi.scanNetworks(true /* async */);
  wifiScanInProgress = true;
  lastScanStartAt = millis();
}

void pollBeaconScan() {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING || n == WIFI_SCAN_FAILED) return;

  wifiScanInProgress = false;

  float bx[BEACON_COUNT], by[BEACON_COUNT], bd[BEACON_COUNT];
  bool seen[BEACON_COUNT] = {};
  int found = 0;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    for (size_t b = 0; b < BEACON_COUNT; b++) {
      if (!seen[b] && ssid == BEACONS[b].ssid) {
        seen[b] = true;
        bx[b] = BEACONS[b].x;
        by[b] = BEACONS[b].y;
        bd[b] = distanceFromRssi(WiFi.RSSI(i));
        found++;
      }
    }
  }
  WiFi.scanDelete();

  if (found >= 3) {
    int idx[3], k = 0;
    for (size_t b = 0; b < BEACON_COUNT && k < 3; b++) {
      if (seen[b]) idx[k++] = b;
    }
    float x, y;
    if (trilaterate(bx[idx[0]], by[idx[0]], bd[idx[0]],
                     bx[idx[1]], by[idx[1]], bd[idx[1]],
                     bx[idx[2]], by[idx[2]], bd[idx[2]], x, y)) {
      mowerX = x;
      mowerY = y;
      havePositionFix = true;
      Serial.printf("Position: (%.2f, %.2f) m [%d beacons]\n", x, y, found);
    } else {
      havePositionFix = false;
      Serial.println("Position: beacons collinear, no fix.");
    }
  } else {
    havePositionFix = false;
    Serial.printf("Position: no fix (%d/3 beacons visible)\n", found);
  }
}

// Latches true on the first e-stop press and stays true until reboot —
// an e-stop that could clear itself isn't a safety feature.
volatile bool estopLatched = false;

void IRAM_ATTR onEstop() {
  estopLatched = true;
}

void setMotor(int in1, int in2, int enPin, int speed) {
  // speed: -255..255, sign is direction, 0 is stopped.
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  analogWrite(enPin, abs(speed));
}

void stopMotors() {
  setMotor(LEFT_IN1, LEFT_IN2, LEFT_EN, 0);
  setMotor(RIGHT_IN1, RIGHT_IN2, RIGHT_EN, 0);
}

void driveForward(int speed) {
  setMotor(LEFT_IN1, LEFT_IN2, LEFT_EN, speed);
  setMotor(RIGHT_IN1, RIGHT_IN2, RIGHT_EN, speed);
}

void driveBackward(int speed) {
  driveForward(-speed);
}

void turnInPlace(int speed) {
  setMotor(LEFT_IN1, LEFT_IN2, LEFT_EN, speed);
  setMotor(RIGHT_IN1, RIGHT_IN2, RIGHT_EN, -speed);
}

void bladeOn()  { digitalWrite(BLADE_RELAY_PIN, HIGH); }
void bladeOff() { digitalWrite(BLADE_RELAY_PIN, LOW); }

// Returns distance in cm, or -1 on timeout (no echo / out of range).
long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long durationUs = pulseIn(ECHO_PIN, HIGH, 30000UL); // ~5m max range
  if (durationUs == 0) return -1;
  return durationUs / 58; // speed of sound round trip
}

void enterState(State next) {
  state = next;
  stateStartedAt = millis();
}

// Averages a few samples to smooth out ADC noise on the divider.
float readBatteryVoltage() {
  const int samples = 8;
  uint32_t total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(BATTERY_ADC_PIN);
    delayMicroseconds(200);
  }
  float adcVoltage = (total / (float)samples) * (ADC_REF_VOLTAGE / ADC_MAX_COUNT);
  return adcVoltage / BATTERY_DIVIDER_RATIO;
}

void setup() {
  Serial.begin(115200);

  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(LEFT_EN, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  pinMode(RIGHT_EN, OUTPUT);
  pinMode(BLADE_RELAY_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(ESTOP_PIN, INPUT);
  analogReadResolution(12);

  // Safe defaults before anything else can run.
  stopMotors();
  bladeOff();

  attachInterrupt(digitalPinToInterrupt(ESTOP_PIN), onEstop, FALLING);

  // STA mode with no AP connection — scanning works standalone.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Mower firmware up. MOWING.");
  enterState(MOWING);
}

void loop() {
  if (estopLatched && state != ESTOPPED) {
    stopMotors();
    bladeOff();
    enterState(ESTOPPED);
    Serial.println("E-STOP triggered. Motors and blade off. Reset board to clear.");
  }

  if (state != ESTOPPED && state != BATTERY_LOW && millis() - lastBatteryCheckAt >= BATTERY_CHECK_MS) {
    lastBatteryCheckAt = millis();
    float voltage = readBatteryVoltage();
    if (voltage < LOW_BATTERY_VOLTAGE) {
      stopMotors();
      bladeOff();
      enterState(BATTERY_LOW);
      Serial.printf("Battery low: %.2fV. Motors and blade off. Recharge and reset board to clear.\n", voltage);
    }
  }

  // Position telemetry runs regardless of mower state — useful even stopped.
  if (!wifiScanInProgress && millis() - lastScanStartAt >= LOCATE_INTERVAL_MS) {
    startBeaconScan();
  }
  if (wifiScanInProgress) {
    pollBeaconScan();
  }

  switch (state) {
    case ESTOPPED:
      // Do nothing, forever, until physically reset.
      return;

    case BATTERY_LOW:
      // Same as ESTOPPED: latched off until the board is reset. Resuming
      // autonomously on a sagging battery risks a brownout mid-mow.
      return;

    case MOWING: {
      bladeOn();
      driveForward(DRIVE_SPEED);
      long distanceCm = readDistanceCm();
      if (distanceCm > 0 && distanceCm < OBSTACLE_CM) {
        Serial.printf("Obstacle at %ld cm. Backing up.\n", distanceCm);
        bladeOff();
        enterState(BACKING_UP);
      }
      break;
    }

    case BACKING_UP:
      driveBackward(DRIVE_SPEED);
      if (millis() - stateStartedAt >= BACKUP_MS) {
        enterState(TURNING);
      }
      break;

    case TURNING:
      turnInPlace(TURN_SPEED);
      if (millis() - stateStartedAt >= TURN_MS) {
        enterState(MOWING);
      }
      break;
  }
}
