#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define LIGHT_PIN 26

// Protocol zone names — must match the Go server and React client.
// Supported zones: "desktop"
static const char* ZONE_DESKTOP = "desktop";

WebSocketsServer ws(81);

void onWebSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type != WStype_TEXT) return;

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

  const char* zone  = doc["zone"]  | "";
  bool        state = doc["state"] | false;

  if (strcmp(zone, ZONE_DESKTOP) == 0) {
    digitalWrite(LIGHT_PIN, state ? HIGH : LOW);
    Serial.printf("desktop -> %s\n", state ? "ON" : "OFF");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());

  ws.begin();
  ws.onEvent(onWebSocketEvent);
  Serial.println("WS server started on port 81");
}

void loop() {
  ws.loop();
}
