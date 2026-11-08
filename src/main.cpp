#include <Arduino.h>
#include <WiFi.h>
#include "cam_server.h"
#include "config.h"

static std::string localIp() { return WiFi.localIP().toString().c_str(); }
static uint32_t nowMs() { return millis(); }

static std::string macSerial() {
  uint64_t mac = ESP.getEfuseMac();
  char b[13];
  snprintf(b, sizeof(b), "%012llx", (unsigned long long)mac);
  return b;
}

static CamConfig makeConfig() {
  CamConfig c;
  c.onvifPort = ONVIF_PORT;
  c.rtspPort = RTSP_PORT;
  c.holdMs = SENSOR_HOLD_MS;
  c.sensorToken = SENSOR_TOKEN;
  c.serial = macSerial();
  return c;
}

static SensorCamServer* server;
static bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("No Wi-Fi settings: copy include/secrets.example.h to include/secrets.h and rebuild.");
    while (true) delay(1000);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save loses multicast and adds latency
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, NTP_SERVER);

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    Serial.println("Could not open the ONVIF or RTSP port; restarting.");
    delay(2000);
    ESP.restart();
  }
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("Connected. ONVIF on %s:%u\n", WiFi.localIP().toString().c_str(), ONVIF_PORT);
    server->networkUp();
  }
  wasConnected = connected;
  server->poll(20);
}
