#include <Arduino.h>
#include <WiFi.h>

// Flash this to each fixed beacon board. Change BEACON_ID per board (1, 2,
// 3, ...) before uploading — it becomes part of the broadcast SSID, which
// mower/src/main.cpp's BEACONS[] table matches against.
#define BEACON_ID 1

void setup() {
  Serial.begin(115200);
  String ssid = "MOWER-BEACON-" + String(BEACON_ID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str());
  Serial.printf("Beacon up. SSID=%s\n", ssid.c_str());
  Serial.println("Measure this board's (x, y) position in meters from your");
  Serial.println("chosen yard origin and enter it in mower/src/main.cpp's BEACONS[].");
}

void loop() {
  // Nothing to do — the AP broadcast itself is all the mower scans for.
  delay(1000);
}
