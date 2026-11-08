#include <Arduino.h>
#include <LittleFS.h>
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
  c.webPassword = WEB_PASSWORD;
  // Wi-Fi and lwIP fail badly when RAM runs out, so a large request must leave them room.
  c.canAllocate = [](size_t n) { return ESP.getMaxAllocHeap() >= n + 1024 && ESP.getFreeHeap() >= n + 16 * 1024; };
  c.fsUsage = [](uint32_t& used, uint32_t& total) {
    used = LittleFS.usedBytes();
    total = LittleFS.totalBytes();
  };
  c.serial = macSerial();
  return c;
}

static SensorCamServer* server;
static bool wasConnected = false;

struct SensorMessage {
  uint8_t mac[6];
  SensorReport report;
};
static QueueHandle_t sensorQueue;

// Runs on the Wi-Fi task, so it only queues. loop() owns all sensor state.
static void onEspNow(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  SensorMessage m;
  if (!parseSensorPacket(data, len, m.report)) return;
  memcpy(m.mac, info->src_addr, 6);
  xQueueSend(sensorQueue, &m, 0);
}

void setup() {
  Serial.begin(115200);
  if (strlen(WIFI_SSID) == 0) {
    // Repeats because native USB serial misses anything printed before the PC reattaches.
    while (true) {
      Serial.println("No Wi-Fi settings: copy include/secrets.example.h to include/secrets.h and rebuild.");
      delay(2000);
    }
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // power save loses multicast and adds latency
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, NTP_SERVER);

  // Formats on first use. Without storage the hub still runs, using its built-in pictures.
  if (!LittleFS.begin(true)) Serial.println("LittleFS could not start; using built-in pictures.");

  server = new SensorCamServer(makeConfig(), localIp, nowMs);
  if (!server->begin()) {
    Serial.println("Could not open the ONVIF or RTSP port; restarting.");
    delay(2000);
    ESP.restart();
  }

  sensorQueue = xQueueCreate(32, sizeof(SensorMessage));
  if (esp_now_init() == ESP_OK) esp_now_register_recv_cb(onEspNow);
  else Serial.println("ESP-NOW failed to start; HTTP sensor input still works.");

  enableLoopWDT();  // an unattended hub must restart itself if the main loop ever stalls
}

void loop() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    server->networkUp();
  }
  wasConnected = connected;

  static uint32_t printedAt = 0;
  if (connected && millis() - printedAt > 10000) {
    printedAt = millis();
    Serial.printf("ONVIF on %s:%u, ESP-NOW to %s on Wi-Fi channel %d, free heap %u, largest block %u, PSRAM free %u, last reset reason %d\n",
                  WiFi.localIP().toString().c_str(), ONVIF_PORT, WiFi.macAddress().c_str(), WiFi.channel(), ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  ESP.getFreePsram(), (int)esp_reset_reason());
  }

  SensorMessage m;
  while (xQueueReceive(sensorQueue, &m, 0) == pdTRUE) server->espNowInput(m.mac, m.report);
  server->poll(20);
}
