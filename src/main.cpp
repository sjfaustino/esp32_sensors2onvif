#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <freertos/queue.h>
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

struct SensorMessage {
  int id;
  bool active;
};
static QueueHandle_t sensorQueue;

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const uint8_t*, const uint8_t* data, int len) {
  SensorMessage m;
  if (parseSensorPacket(data, len, m.id, m.active)) xQueueSend(sensorQueue, &m, 0);
}

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

  sensorQueue = xQueueCreate(32, sizeof(SensorMessage));
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onEspNow);
  else Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("Connected. ONVIF on %s:%u\n", WiFi.localIP().toString().c_str(), ONVIF_PORT);
    Serial.printf("ESP-NOW: send to %s on Wi-Fi channel %d\n", WiFi.macAddress().c_str(), WiFi.channel());
    server->networkUp();
  }
  wasConnected = connected;

  SensorMessage m;
  while (xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->sensorInput(m.id, m.active);
  server->poll(20);
}
