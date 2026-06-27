#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define LIGHT_PIN  26
#define MQTT_PORT  1883

// Protocol topic names — must match the Go server.
static const char* TOPIC_DESKTOP = "lights/desktop";

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

void onMessage(char* topic, byte* payload, unsigned int length) {
  bool state = (length > 0 && payload[0] == '1');
  if (strcmp(topic, TOPIC_DESKTOP) == 0) {
    digitalWrite(LIGHT_PIN, state ? HIGH : LOW);
    Serial.printf("desktop -> %s\n", state ? "ON" : "OFF");
  }
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect("esp32-control-panel")) {
      Serial.println("ok");
      mqtt.subscribe(TOPIC_DESKTOP);
    } else {
      Serial.printf("failed rc=%d, retry in 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);

  Serial.printf("MAC:      %s\n", WiFi.macAddress().c_str());
  Serial.printf("SSID:     [%s]\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 20000) {
      wl_status_t s = WiFi.status();
      const char* reason =
        s == WL_NO_SSID_AVAIL   ? "SSID not found" :
        s == WL_CONNECT_FAILED  ? "wrong password / auth rejected" :
        s == WL_CONNECTION_LOST ? "connection lost" :
                                  "unknown";
      Serial.printf("\nFailed (status=%d: %s). Restarting...\n", s, reason);
      delay(1000);
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
    if (++dots % 10 == 0)
      Serial.printf(" status=%d\n", WiFi.status());
  }
  Serial.printf("\nConnected. IP: %s  RSSI: %d dBm  BSSID: %s\n",
    WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.BSSIDstr().c_str());

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMessage);
  reconnectMQTT();
  Serial.println("MQTT ready");
}

void loop() {
  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();
}
