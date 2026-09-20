#include <Arduino.h>

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
