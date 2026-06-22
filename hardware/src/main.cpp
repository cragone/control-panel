#include <ESP32Servo.h>

Servo myServo;
int servoPin = 26;

void setup() {
  Serial.begin(115200);
  myServo.attach(servoPin);
  Serial.println("Setup done");
}

void loop() {
  Serial.println("Sweep up");
  for (int pos = 0; pos <= 180; pos++) {
    myServo.write(pos);
    delay(15);
  }

  Serial.println("Sweep down");
  for (int pos = 180; pos >= 0; pos--) {
    myServo.write(pos);
    delay(15);
  }
}
