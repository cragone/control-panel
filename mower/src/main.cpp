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

// ---- Tuning ---------------------------------------------------------------
#define DRIVE_SPEED       180   // 0-255
#define TURN_SPEED        160
#define OBSTACLE_CM       35    // stop/avoid distance
#define BACKUP_MS         600
#define TURN_MS           500

enum State { MOWING, BACKING_UP, TURNING, ESTOPPED };
static State state = MOWING;
static unsigned long stateStartedAt = 0;

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

  switch (state) {
    case ESTOPPED:
      // Do nothing, forever, until physically reset.
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
